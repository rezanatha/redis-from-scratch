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

static int respond_client(int connfd){
	char read_buffer[32] = {};
	if(read(connfd, read_buffer, sizeof(read_buffer)-1) < 0) {
		printf("read() error %s... \n", strerror(errno));
		return -1;
	}
	printf("Client says: %s \n", read_buffer);
	char write_buffer[] = "+PONG\r\n";
	write(connfd, write_buffer, strlen(write_buffer));

	return 0;
}

const size_t k_max_msg = 4096;

static int32_t read_full (int fd, char* buf, size_t n) {
	// read() will read all data that is available on the kernel
	// read_full() ensures we only read n bytes of data
    while (n > 0) {
		ssize_t rv = read(fd, buf, n);
		printf("rv: %ld\n", rv);
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

static int32_t one_request (int connfd) {
	// read 4 bytes header
	char rbuf[4 + k_max_msg + 1] = {};

	errno = 0;
	int32_t err = read_full(connfd, rbuf, 4); /* reading 4 bytes */
	
	if (err) {
		if (errno == 0) {
			msg("EOF");
		} else {
			msg("read() error ");
		}

		return err;
	}
	uint32_t len = 0;
	memcpy(&len, rbuf, 4); //assuming little endian
	printf("len: %d\n", len);
	if (len > k_max_msg) {
		msg("too long");
		return -1;
	}

	// request body
	err = read_full(connfd, &rbuf[4], len);
	if (err) {
		msg("read() error");
		return err;
	}

	// do something
	rbuf[4 + len] = '\0';
	printf("Client says: %s\n", &rbuf[4]);


    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
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
	
	while (1) {
	    //Property of our client address
	    struct sockaddr_in client_addr = {}; 
	    
	    socklen_t client_addr_len = sizeof(client_addr);
	    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
	    if (client_fd < 0) { 
	    	//printf("Accept failed: %s \n", strerror(errno));
	    	continue;
	    }
	    printf("Client connected\n");
        while (1) {
			int32_t err = one_request(client_fd);
			if (err) {
	    	    //printf("Unable to respond to client properly \n");
				break;
	        } 
		}
		close(client_fd);
	}
	
	return 0;
}
