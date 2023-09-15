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

const size_t k_max_msg = 4096;
static int32_t query (int fd, const char* text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        return -1;
    }
    // write
    char wbuf[4+k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);

    int32_t err = write_all(fd, wbuf, 4 + len);

    if (err) {
        return err;
    }

    //4 bytes header
    char rbuf[4+k_max_msg+1];
    errno = 0;
    err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }    

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

    //do something
	rbuf[4 + len] = '\0';
	printf("Server says: %s\n", &rbuf[4]);
    return 0;

}

int talk (int connfd, const char* message) {
    //talk with protocols. first 4 bytes are headers: message length. remaining bytes are messages
    //char message[] = "+PING\r\n";
    uint32_t len = strlen(message);
    char write_buffer[32] = {};
    memcpy(write_buffer, &len, 4);
    memcpy(&write_buffer[4], message, len);

	int32_t wbytes = write_all(connfd, write_buffer, 4+len);
    if (wbytes < 0) {
        errmsg("write error");
        return -1;
    }

	char read_buffer[32] = {};
	if(read_full(connfd, read_buffer, 4+len) < 0) {
        errmsg("read error");
		return -1;
	}
    int32_t rlen;
	memcpy(&rlen, read_buffer, 4);
	printf("Server: HEADER: %d MESSAGE: %s \n", rlen, &read_buffer[4]);

    return 0;
}

int main() {
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

    talk(client_fd, "+PING\r\n");

//     int32_t err = query(client_fd, "hello1");
//     if (err) {
//         goto L_DONE;
//     }
//     err = query(client_fd, "hello2");
//     if (err) {
//         goto L_DONE;
//     }
//     err = query(client_fd, "hello there baby");
//     if (err) {
//         goto L_DONE;
//     }

// L_DONE:
    close(client_fd);
    return 0;
}