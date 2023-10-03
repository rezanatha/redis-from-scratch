#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <vector>
#include <string>
#include <map>

const size_t k_max_msg = 4096;

static void msg (const char* msg) {
	fprintf(stderr, "%s\n", msg);
}

static void errmsg (const char* msg) {
	fprintf(stderr, "[%d] %s ... %s\n", errno, strerror(errno), msg);
	abort();
}

enum { //state to define what to do with connection
    STATE_REQ = 0, //reading requests
    STATE_RES = 1, //sending responses
    STATE_END = 2, //mark for deletion
};

struct Conn {
    int fd = -1;
    uint32_t state = 0;
	//buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
	uint8_t* rbuf_ptr = NULL;
	//buffer for writing
    size_t wbuf_size = 0;
	size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void fd_set_nb (int fd) {
	errno = 0;
	int flags = fcntl(fd, F_GETFL, 0);
	if (errno) {
		errmsg("fcntl error");
		return;
	}
	flags |= O_NONBLOCK;

	errno = 0;
	(void)fcntl(fd, F_SETFL, flags);
	if (errno) {
		errmsg("fcntl error");
	}

}

static void conn_put (std::vector<Conn*> &fd2conn, struct Conn* conn) {
	if(fd2conn.size() <= (size_t)conn->fd) {
		fd2conn.resize(conn->fd + 1);
	}
	fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn (std::vector<Conn *> &fd2conn, int fd) {
	// accept
	struct sockaddr_in client_addr = {};
	socklen_t socklen = sizeof(client_addr);
	int connfd = accept(fd, (struct sockaddr*) &client_addr, &socklen);
	if (connfd < 0) {
		//msg("accept() error");
		return -1;
	}
	//printf("connfd %d \n", connfd);
	//fd_set_nb(connfd);
	struct Conn* conn = (struct Conn*)malloc(sizeof(struct Conn));
	if (!conn) {
		close(connfd);
		return -1;
	}
	conn->fd = connfd;
	conn->state = STATE_REQ;
	conn->rbuf_size = 0;
	conn->wbuf_size = 0;
	conn->wbuf_sent = 0;
	conn_put(fd2conn, conn);
	return 0;
}

static void state_req(Conn* conn);
static void state_res(Conn* conn);
const size_t k_max_args = 1024;

static int parse_req (const uint8_t *data, 
    size_t len, std::vector<std::string> &out)
{
	if (len < 4) {
		return -1;
	}
	uint32_t n = 0;
	memcpy(&n, &data[0], 4);
	if(n > k_max_args) {
		return -1;
	}

	size_t pos = 4;
	while (n--) {
		if (pos+4  > len) {
			return -1;
		}
		uint32_t sz = 0;
		memcpy(&sz, &data[pos], 4);
		if (pos + 4 + sz > len) {
			return -1;
		}
		out.push_back(std::string((char *)&data[pos + 4], sz));
		pos += 4 + sz;
	}
	if (pos != len) {
        return -1;  // trailing garbage
    }
    return 0;
}
enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

// The data structure for the key space. This is just a placeholder
// until we implement a hashtable in the next chapter
static std::map<std::string, std::string> g_map;

static uint32_t do_get(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    if (!g_map.count(cmd[1])) {
        return RES_NX;
    }
    std::string &val = g_map[cmd[1]];
    assert(val.size() <= k_max_msg);
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();
    return RES_OK;
}

static uint32_t do_set(
	const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
	(void)res;
	(void)reslen;
	g_map[cmd[1]] = cmd[2];
	return RES_OK;
}

static uint32_t do_del(
	const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
	(void)res;
	(void)reslen;
	g_map.erase(cmd[1]);
	return RES_OK;
}

static bool cmd_is(const std::string &word, const char *cmd) {
    return 0 == strcasecmp(word.c_str(), cmd);
}

static int32_t do_request (
	const uint8_t *req, uint32_t reqlen,
	uint32_t *rescode, uint8_t *res, uint32_t *reslen)
{
	std::vector<std::string> cmd;
	if (0 != parse_req(req, reqlen, cmd)) {
		msg("bad request");
		return -1;
	}
	if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
		*(rescode) = do_get(cmd, res, reslen);
	} else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
		*(rescode) = do_set(cmd, res, reslen);
	} else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
		*(rescode) = do_del(cmd, res, reslen);
	} else {
		*(rescode)= RES_ERR;
		const char msg[] = "unknown command";
		strcpy((char*)res, msg);
		*(reslen) = strlen(msg);
	}
	return 0;

}

static bool try_one_request (Conn* conn) {
	if (conn->rbuf_size < 4) {
		return false;
	}
	uint32_t len = 0;
	memcpy(&len, conn->rbuf_ptr, 4);
	if (len > k_max_msg) {
		msg("too long");
		conn->state = STATE_END;
		return false;
	}

	if (4 + len > conn->rbuf_size) {
		return false;
	}

	//printf("Client says %.*s \n", len, conn->rbuf_ptr + 4);
	uint32_t rescode = 0;
	uint32_t wlen = 0;
	int32_t err = do_request(conn->rbuf_ptr+4, len, &rescode, &conn->wbuf[4 + 4],&wlen);

	if (err) {
		conn->state = STATE_END;
		return false;
	}

	wlen += 4;
	memcpy(&conn->wbuf[conn->wbuf_size], &wlen, 4);
	memcpy(&conn->wbuf[conn->wbuf_size+4], &rescode, 4);
	conn->wbuf_size += 4 + wlen;

	//change rbuf_size to remaining bytes, move rbuf pointer forward 4 + length of current message steps
	conn->rbuf_size = (size_t)(conn->rbuf_size - (4 + len));
	conn->rbuf_ptr += (4+len);
	assert(conn->rbuf_ptr);

	// multiple write calls, change state to responding (STATE_RES)
	//conn->state = STATE_RES;
	//state_res(conn);

	return (conn->state == STATE_REQ);
}

static bool try_fill_buffer (Conn* conn) {
	assert(conn->rbuf_size < sizeof(conn->rbuf));
	ssize_t rv = 0;
	do {
		size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
		rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
		//printf("rv: %lu \n", rv);
	} while (rv < 0 && errno == EINTR);

	if (rv < 0 && errno == EAGAIN) {
		// got EAGAIN, stop.
		return false;
	}

	if (rv < 0) {
		msg("read() error");
		conn->state = STATE_END;
		return false;
	}
	
	if (rv == 0) {
		if (conn->rbuf_size > 0) {
			msg("unexpected EOF");
		} else {
			msg("EOF");
		}
		conn->state = STATE_END;
		return false;
	}

	conn->rbuf_size += (size_t)rv;
	assert(conn->rbuf_size <= sizeof(conn->rbuf));
     
	//set read pointer
	conn->rbuf_ptr = &conn->rbuf[0];

	while(try_one_request(conn)){}

	//single buffered write call, change state to responding (STATE_RES)
	//printf("fd: %d, wbuf size: %zu\n", conn->fd, conn->wbuf_size);
	conn->state = STATE_RES;
	state_res(conn);

	return (conn->state == STATE_REQ);
}

static bool try_flush_buffer (Conn* conn) {
	ssize_t rv = 0;
	do {
		size_t remain = conn->wbuf_size - conn->wbuf_sent;
		uint8_t *loc = &conn->wbuf[conn->wbuf_sent];
		rv = write(conn->fd, loc, remain);
	} while (rv < 0 && errno == EINTR);

	if (rv < 0 && errno == EAGAIN) {
		//got EAGAIN, stop
	    return false;
	}

	if (rv < 0) {
		msg("write() error");
		conn->state = STATE_END;
		return false;
	}

	conn->wbuf_sent += (size_t)rv;
	assert(conn->wbuf_sent <= conn->wbuf_size);
	if (conn->wbuf_sent == conn->wbuf_size) {
		//response was fully sent, change state
		conn->state = STATE_REQ;
		conn->wbuf_sent = 0;
		conn->wbuf_size = 0;
		return false;
	}

	//still got data in wbuf, could try to write again
	return true;
}

static void state_req (Conn* conn) {
	while(try_fill_buffer(conn)) {}
}

static void state_res (Conn* conn) {
	while (try_flush_buffer(conn)){};
}


static void connection_io (Conn* conn) {
	if (conn->state == STATE_REQ) {
		state_req(conn);
	} else if (conn->state == STATE_RES) {
		state_res(conn);
	} else {
		assert(0);
	}
}

int main (int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	// Get an fd for stream socket in the internet domain
	// fd = file descriptor, refers to something in an unix kernel (e.g., TCP connection, file, listening port)
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		errmsg("Socket creation failed");
		return 1;
	}

	// Since the tester restarts your program quite often, setting REUSE_PORT
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		errmsg("SO_REUSEPORT failed");
		return 1;
	}
	
	// Property of our server address
	struct sockaddr_in serv_addr = { .sin_family = AF_INET,
									 .sin_port = htons(6379),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	//bind
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		errmsg("Bind failed");
		return 1;
	}

    //listen
	if (listen(server_fd, SOMAXCONN) != 0) {
		errmsg("Listen failed");
		return 1;
	}
	
	printf("Waiting for client(s) to connect...\n");

	//map of all client connections, keyed by fd
	std::vector<Conn*> fd2conn;
	//fd_set_nb(server_fd);

	int kq;
	if((kq = kqueue())  == -1) {
		errmsg("error kqueue()");
	}

	struct kevent change_list[10];
	struct kevent ev_list[10];
	EV_SET(change_list, server_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	int num_change_list = kevent(kq, change_list, 1, NULL, 0, NULL);
	if (num_change_list < 0) {
		errmsg("server kevent()");
	}

	while (1) {
		//check for events, do not add to kqueue() first
		struct timespec timeout = {1,0};
		int num_ev = kevent(kq, NULL, 0, ev_list, 2, &timeout);
		if (num_ev < 0) {
			errmsg("kevent()");
		}
		for (int i = 0; i < num_ev; ++i) {
			int event_fd = ev_list[i].ident;
			if (ev_list[i].flags & EV_ERROR) {
				errmsg("EV_ERROR on event_fd");
			} 
			else if (event_fd == server_fd) {
				//if event wants to connect to our socket, accept it
				(void)accept_new_conn(fd2conn, event_fd);
			}
		}

		//handle accepted connections
		for(Conn* conn: fd2conn) {
			if(!conn) {
				continue;
			}
			//add accepted connection to the kqueue
	        int16_t filter = (conn->state == STATE_REQ) ? EVFILT_READ: EVFILT_WRITE;
			EV_SET(change_list, conn->fd, filter, EV_ADD, 0, 0, NULL);
			int ev_num = kevent(kq, change_list, 1, NULL, 0, NULL);
			if (ev_num < 0) {
				errmsg("kevent() error");
			}

			//process connection
			connection_io(conn);

			//if client ends connection then disconnect
			if (conn->state == STATE_END) {
			    fd2conn[conn->fd] = NULL;
				EV_SET(change_list, conn->fd, EVFILT_PROC, EV_DELETE, 0, 0, NULL);
                kevent(kq, change_list, 1, NULL, 0, NULL);
			    (void)close(conn->fd);
			    free(conn);
			}
		}
	}
	
	return 0;
}
