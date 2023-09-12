#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

int respond_client(int connfd){
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

int main() {
	// Disable output buffering
	setbuf(stdout, NULL);

	// Get an fd for stream socket in the internet domain
	// fd = file descriptor, refers to something in an unix kernel (e.g., TCP connection, file, listening port)
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting REUSE_PORT
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return 1;
	}
	
	// Property of our server address
	struct sockaddr_in serv_addr = { .sin_family = AF_INET,
									 .sin_port = htons(6379),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	if (listen(server_fd, SOMAXCONN) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
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
    
	    if (respond_client(client_fd) < 0) {
	    	printf("Unable to respond to client properly \n");
	    }  
	}
	close(server_fd);
	return 0;
}
