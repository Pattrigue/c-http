#include <err.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#define PORT 8080

int main(void) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        err(EXIT_FAILURE, "Socket creation failed");
    }

    struct sockaddr_in my_addr = {
        .sin_family = AF_INET,
        .sin_addr = {.s_addr = htonl(INADDR_ANY)},
        .sin_port = htons(PORT),
    };

    int ret = bind(socket_fd, (struct sockaddr *)&my_addr, sizeof(my_addr));

    if (ret == -1) {
        err(EXIT_FAILURE, "Bind failed");
    }

    ret = listen(socket_fd, 10);

    if (ret == -1) {
        err(EXIT_FAILURE, "Listen failed");
    }

    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    printf("Listening on port %d.\n", PORT);

    int connection_fd = accept(
        socket_fd,
        (struct sockaddr *)&peer_addr,
        &peer_addr_len
    );

    if (connection_fd == -1) {
        err(EXIT_FAILURE, "Accept failed");
    }

    return 0;
}
