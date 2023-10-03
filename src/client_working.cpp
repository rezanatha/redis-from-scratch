#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <vector>
#include <string>

const size_t k_max_msg = 4096;

static void msg (const char* msg) {
	fprintf(stderr, "%s\n", msg);
}

static void errmsg (const char* msg) {
	fprintf(stderr, "[%d] %s ... %s\n", errno, strerror(errno) ,msg);
}

static int32_t read_full (int fd, char* buf, size_t n) {
    while (n > 0) {
		ssize_t rv = read(fd, buf, n);
		if (rv <= 0) {
			return -1;
		}
		assert((size_t)rv <= n);
		n -= (size_t)rv;
		buf += rv;
	}
	return 0;
}

static int32_t write_all (int fd, const char* buf, size_t n) {
	while (n > 0) {
		ssize_t rv = write(fd, buf, n);
		if (rv <= 0) {
			return -1;
		}
		assert((size_t)rv <= n);
		n -= (size_t)rv;
		buf += rv;
	}
	return 0;
}

static int32_t send_req (int fd, const std::vector<std::string> &cmd) {
    uint32_t len = 4;
    for(const std::string &s: cmd) {
        len += 4 + s.size();
    }
    if (len > k_max_msg) {
        return -1;
    }

    // write
    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &len, 4);
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;
    for (const std::string &s: cmd) {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    int32_t err = write_all(fd, wbuf, 4 + len);
    if (err) {
        return err;
    }
    return 0;
}

static int32_t read_res (int fd) {
    //read
    //4 bytes header
    char rbuf[4+k_max_msg+1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }    
    uint32_t len = 0;
	memcpy(&len, rbuf, 4); //assuming little endian
	if (len > k_max_msg) {
		msg("too long");
		return -1;
	}

    //reply body
    err = read_full(fd, &rbuf[4], len);
	if (err) {
		msg("read() error");
		return err;
	}

    //do something, print the result
    uint32_t rescode = 0;
    memcpy(&rescode, &rbuf[4], 4);
    printf("Server says: [%u] %.*s \n", rescode, len-4, &rbuf[8]);
    return 0;

}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        //printf("Socket creation failed: %s...\n", strerror(errno));
        errmsg("Socket creation failed");
		return 1;
    }

	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(6379),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
    
    int rv = connect(client_fd, (const struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (rv) {
        //printf("Unable to connect to server: %d %s...\n", errno, strerror(errno));
        errmsg("Unable to connect to server");
        return 1;
    }
    printf("Connected to server. \n");

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i) {
        cmd.push_back(argv[i]);
    }
    int32_t err = send_req(client_fd, cmd);
    if (err) {
        goto L_DONE;
    }
    err = read_res(client_fd);
    if (err) {
        goto L_DONE;
    }

L_DONE:
    close(client_fd);
    return 0;
}