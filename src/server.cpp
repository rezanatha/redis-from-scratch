#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <signal.h>
#include <vector>

const size_t k_max_msg = 4096;

static void msg (const char* msg) {
	fprintf(stderr, "%s\n", msg);
}

static void errmsg (const char* msg) {
	fprintf(stderr, "[%d] %s ... %s\n", errno, strerror(errno) ,msg);
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
		msg("accept() error");
		return -1;
	}

	fd_set_nb(connfd);
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

static bool try_one_request (Conn* conn) {
	if (conn->rbuf_size < 4) {
		return false;
	}
	uint32_t len = 0;
	memcpy(&len, &conn->rbuf[0], 4);
	if (len > k_max_msg) {
		msg("too long");
		conn->state = STATE_END;
		return false;
	}

	if (4 + len > conn->rbuf_size) {
		return false;
	}

	printf("Client says %.*s \n", len, &conn->rbuf[4]);

	memcpy(&conn->wbuf[0], &len, 4);
	memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
	conn->wbuf_size = 4 + len;

	size_t remain = conn->rbuf_size - 4 - len;
	if (remain) {
		memmove(conn->rbuf, &conn->rbuf[4+len], remain);
	}
	conn->rbuf_size = remain;

	//change state
	conn->state = STATE_RES;
	state_res(conn);

	return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn* conn) {
	assert(conn->rbuf_size < sizeof(conn->rbuf));
	ssize_t rv = 0;
	do {
		size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
		rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
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

	while(try_one_request(conn)) {}
	return (conn->state == STATE_REQ);
}

static bool try_flush_buffer (Conn* conn) {
	ssize_t rv = 0;
	do {
		size_t remain = conn->wbuf_size - conn->wbuf_sent;
		rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
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
static void state_req(Conn* conn) {
	while(try_fill_buffer(conn)) {}
}

static void state_res (Conn* conn) {
	while (try_flush_buffer(conn)) {}
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

int main() {
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
	
	printf("Waiting for a client to connect...\n");

	//map of all client connections, key: fd
	std::vector<Conn*> fd2conn;
	fd_set_nb(server_fd);
	std::vector<struct pollfd> poll_args;
	
	while (1) {
		poll_args.clear();
		struct pollfd pfd = {server_fd, POLLIN, 0};
		poll_args.push_back(pfd);

		for (Conn* conn: fd2conn) {
			if (!conn) {
			 	continue;
			}
			struct pollfd pfd = {};
			pfd.fd = conn->fd;
			pfd.events = (conn->state == STATE_REQ) ? POLLIN: POLLOUT;
			pfd.events = pfd.events | POLLERR;
			poll_args.push_back(pfd);
		}

		int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
		if (rv < 0) {
			errmsg("poll");
		}
		//printf("poll() %d size: %zu \n", rv, poll_args.size());
		for(size_t i = 1; i < poll_args.size(); ++i) {
			//printf("process connection %d \n",poll_args[i].revents);
			if (poll_args[i].revents) {
				Conn* conn = fd2conn[poll_args[i].fd];
				
				connection_io(conn);
			
			    if (conn->state == STATE_END) {
			    	fd2conn[conn->fd] = NULL;
			    	(void)close(conn->fd);
			    	free(conn);
			    }
			}
		}

		if (poll_args[0].revents) {
			//printf("accept new connection \n");
			(void)accept_new_conn(fd2conn, server_fd);
		}
		}
	
	return 0;
}
