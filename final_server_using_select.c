#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

typedef struct {
    int socket;
    char name[50];
    char IP[16];
    int portno;
} Client;

Client clients[MAX_CLIENTS];
fd_set active_fds, read_fds;
int max_fd;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void send_acknowledgment(int sockfd, const char *message) {
    send(sockfd, message, strlen(message), 0);
}

void send_message(char *recipient, char *message, char *sender, int sender_socket) {
    if (strcmp(recipient, "Group") == 0) {
        // Broadcast to all clients except sender
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket != 0 && clients[i].socket != sender_socket) {
                char msg[BUFFER_SIZE];
                snprintf(msg, sizeof(msg), "[Group] %s: %s", sender, message);
                send(clients[i].socket, msg, strlen(msg), 0);
            }
        }
        // Send acknowledgment to sender
        send_acknowledgment(sender_socket, "Message delivered to group.\n");
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0 && strcmp(clients[i].name, recipient) == 0) {
            char msg[BUFFER_SIZE];
            snprintf(msg, sizeof(msg), "%s: %s", sender, message);
            send(clients[i].socket, msg, strlen(msg), 0);
            // Send acknowledgment to sender
            send_acknowledgment(sender_socket, "Message delivered successfully.\n");
            return;
        }
    }
    send(sender_socket, "Recipient not found.\n", 19, 0);
}

void send_active_clients(int sockfd) {
    char active_clients[BUFFER_SIZE] = "\nActive Users:\n";
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0) {
            strcat(active_clients, clients[i].name);
            strcat(active_clients, "\n");
        }
    }
    send(sockfd, active_clients, strlen(active_clients), 0);
    send_acknowledgment(sockfd, "Active users list sent.\n");
}

void handle_client_message(int sockfd) {
    char buffer[BUFFER_SIZE];
    ssize_t n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
    if (n <= 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket == sockfd) {
                printf("%s disconnected\n", clients[i].name);
                close(sockfd);
                FD_CLR(sockfd, &active_fds);
                clients[i].socket = 0;
                break;
            }
        }
        return;
    }

    buffer[n] = '\0';
    if (strcmp(buffer, "GET_ACTIVE_USERS") == 0) {
        send_active_clients(sockfd);
        return;
    }

    char recipient[50], message[BUFFER_SIZE];
    char *colon_pos = strchr(buffer, ':');
    if (!colon_pos) {
        send(sockfd, "Invalid format (use Recipient:Message)\n", 38, 0);
        return;
    }

    *colon_pos = '\0';
    strcpy(recipient, buffer);
    strcpy(message, colon_pos + 1);

    char *msg_ptr = message;
    while (*msg_ptr == ' ') msg_ptr++;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == sockfd) {
            send_message(recipient, msg_ptr, clients[i].name, sockfd);
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int sockfd, portno = atoi(argv[1]);
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, MAX_CLIENTS);
    printf("Server listening on port %d...\n", portno);

    FD_ZERO(&active_fds);
    FD_SET(sockfd, &active_fds);
    max_fd = sockfd;

    while (1) {
        read_fds = active_fds;
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
            error("ERROR on select");

        for (int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == sockfd) {
                    int newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
                    if (newsockfd < 0) error("ERROR on accept");

                    char name[50];
                    int bytes_received = recv(newsockfd, name, sizeof(name) - 1, 0);
                    if (bytes_received <= 0) {
                        close(newsockfd);
                        continue;
                    }
                    name[bytes_received] = '\0';

                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].socket == 0) {
                            clients[j].socket = newsockfd;
                            strcpy(clients[j].name, name);
                            strcpy(clients[j].IP, inet_ntoa(cli_addr.sin_addr));
                            clients[j].portno = ntohs(cli_addr.sin_port);
                            printf("%s connected from IP: %s\n", name, clients[j].IP);
                            break;
                        }
                    }
                    FD_SET(newsockfd, &active_fds);
                    if (newsockfd > max_fd) max_fd = newsockfd;
                } else {
                    handle_client_message(i);
                }
            }
        }
    }
    close(sockfd);
    return 0;
}
