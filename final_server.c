#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/time.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define IDLE_TIMEOUT 300  // 5 minutes timeout

// Structure to store client information
typedef struct {
    int socket;
    char name[50];
    char IP[16];
    int portno;
} Client;

Client clients[MAX_CLIENTS];  // Array to hold client details
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex to manage access to the client list

// Function to display error message
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Convert string to lowercase
void to_lowercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

// Function to send message to a specific client or group
void send_message(char *recipient, char *message, char *sender) {
    pthread_mutex_lock(&clients_mutex);  // Lock the client list to prevent race conditions

    char recipient_lower[50];
    strcpy(recipient_lower, recipient);
    to_lowercase(recipient_lower);  // Convert recipient name to lowercase for case-insensitivity

    int sender_socket = -1;
    int recipient_found = 0;

    // Find sender's socket
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0 && strcmp(clients[i].name, sender) == 0) {
            sender_socket = clients[i].socket;
            break;
        }
    }

    // If recipient is 'group', send the message to all clients
    if (strcmp(recipient_lower, "group") == 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket != 0 && strcmp(clients[i].name, sender) != 0) {
                char msg[BUFFER_SIZE];
                snprintf(msg, sizeof(msg), "%s: %s", sender, message);
                send(clients[i].socket, msg, strlen(msg), 0);
                recipient_found = 1;
            }
        }
        if (recipient_found) {
            send(sender_socket, "Message delivered to group.", 27, 0);
        } else {
            send(sender_socket, "No active clients to receive message.", 36, 0);
        }
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    // Otherwise, send the message to the specific recipient
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0 && strcmp(clients[i].name, recipient) == 0) {
            char msg[BUFFER_SIZE];
            snprintf(msg, sizeof(msg), "%s: %s", sender, message);
            send(clients[i].socket, msg, strlen(msg), 0);
            recipient_found = 1;
            send(sender_socket, "Message delivered successfully.", 30, 0);
            break;
        }
    }

    // If recipient not found, send error to sender
    if (!recipient_found) {
        send(sender_socket, "Error: Recipient not found.", 28, 0);
    }

    pthread_mutex_unlock(&clients_mutex);  // Unlock the client list
}

// Function to send list of active clients
void send_active_clients(int sockfd) {
    pthread_mutex_lock(&clients_mutex);  // Lock the client list to prevent race conditions
    char active_clients[BUFFER_SIZE] = "Active Users:\n";
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0) {
            strncat(active_clients, clients[i].name, sizeof(active_clients) - strlen(active_clients) - 1);
            strncat(active_clients, "\n", sizeof(active_clients) - strlen(active_clients) - 1);
        }
    }
    pthread_mutex_unlock(&clients_mutex);  // Unlock the client list
    send(sockfd, active_clients, strlen(active_clients), 0);  // Send the active clients list to the requesting client
}

// Function to handle individual client connections
void *handle_client(void *arg) {
    int sockfd = *(int *)arg;  // Get the client's socket descriptor
    free(arg);

    // Set timeout for client inactivity (5 minutes)
    struct timeval timeout;
    timeout.tv_sec = IDLE_TIMEOUT;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char name[50], buffer[BUFFER_SIZE];

    // Receive client's name
    bzero(name, sizeof(name));
    if (recv(sockfd, name, sizeof(name) - 1, 0) <= 0) {
        close(sockfd);
        return NULL;
    }
    name[strcspn(name, "\r\n")] = 0;

    // Store client information in the client list
    pthread_mutex_lock(&clients_mutex);
    int index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == 0) {
            clients[i].socket = sockfd;
            strncpy(clients[i].name, name, sizeof(clients[i].name) - 1);
            clients[i].name[sizeof(clients[i].name) - 1] = '\0';
            index = i;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (index == -1) {
        send(sockfd, "Server full", 11, 0);
        close(sockfd);
        return NULL;
    }

    // Get client's IP and port
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    getpeername(sockfd, (struct sockaddr*)&cli_addr, &cli_len);
    inet_ntop(AF_INET, &cli_addr.sin_addr, clients[index].IP, INET_ADDRSTRLEN);
    clients[index].portno = ntohs(cli_addr.sin_port);

    printf("%s connected from IP: %s\n", name, clients[index].IP);

    // Handle client messages
    while (1) {
        bzero(buffer, BUFFER_SIZE);
        ssize_t n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        
        if (n == 0) {  // If client disconnects
            printf("%s disconnected\n", name);
            break;
        } else if (n < 0) {  // If client disconnects due to inactivity
            printf("\n%s disconnected due to inactivity\n", name);
            break;
        }

        buffer[n] = '\0';

        // Check for "GET_ACTIVE_USERS" command
        if (strcmp(buffer, "GET_ACTIVE_USERS") == 0) {
            send_active_clients(sockfd);
            continue;
        }

        char recipient[50], message[BUFFER_SIZE];
        // Split the message into recipient and message
        char *colon_pos = strchr(buffer, ':');
        if (colon_pos) {
            *colon_pos = '\0';  // Separate recipient from the message
            strcpy(recipient, buffer);
            strcpy(message, colon_pos + 1);
            printf("Message from %s to %s: %s\n", name, recipient, message);
            send_message(recipient, message, name);  // Send the message
        } else {
            send(sockfd, "Invalid format (use Recipient:Message)", 37, 0);  // Invalid message format
        }
    }

    // Remove client from the list when disconnected
    pthread_mutex_lock(&clients_mutex);
    clients[index].socket = 0;
    pthread_mutex_unlock(&clients_mutex);

    close(sockfd);  // Close the socket
    return NULL;
}

// Main function to start the server
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int sockfd, portno = atoi(argv[1]);
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    // Create server socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    // Set up server address structure
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    // Bind the socket to the port
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    // Listen for incoming connections
    listen(sockfd, 5);
    printf("Main Server listening on port %d...\n", portno);

    // Accept client connections in a loop
    while (1) {
        int *newsockfd = malloc(sizeof(int));
        *newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (*newsockfd < 0) {
            free(newsockfd);
            error("ERROR on accept");
        }

        // Create a new thread to handle the client
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, (void *)newsockfd);
        pthread_detach(thread_id);  // Detach the thread so it can clean up itself
    }

    close(sockfd);  // Close the server socket
    return 0;
}
