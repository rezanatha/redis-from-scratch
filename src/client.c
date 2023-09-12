#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int talk (int connfd) {
    char write_buffer[] = "+PING\r\n";
	write(connfd, write_buffer, strlen(write_buffer));
	char read_buffer[32] = {};
	if(read(connfd, read_buffer, sizeof(read_buffer)-1) < 0) {
		printf("read() error %s... \n", strerror(errno));
		return -1;
	}
	printf("Server says: %s \n", read_buffer);
    return 0;
}

int main() {
    setbuf(stdout, NULL);
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
    }

	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(6379),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
    
    int rv = connect(client_fd, (const struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (rv) {
        printf("Unable to connect to server: %s...\n", strerror(errno));
        return 1;
    }
    printf("Connected to server. \n");

    talk(client_fd);
    close(client_fd);
    return 0;
}