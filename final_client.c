#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>  // Include time library

#define BUFFER_SIZE 1024
#define MAX_BUFFERED_MSGS 10 // Store up to 10 messages while typing

// Function to print error and exit
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Global variables
int sockfd;                      // Socket file descriptor
pthread_mutex_t lock;            // Mutex for thread synchronization
volatile int is_typing = 0;      // Flag to track if the user is typing
volatile int skip_prompt_once = 0; // Flag to skip prompt once
volatile int unread_count = 0;    // Counter for unread messages

// Message buffer to store messages while typing
char message_buffer[MAX_BUFFERED_MSGS][BUFFER_SIZE];
int msg_count = 0; // Counter for stored messages

// Function to get the current time in HH:MM:SS format
void get_time(char *buffer, size_t len) {
    time_t raw_time;
    struct tm *time_info;

    // Get current time
    time(&raw_time);
    time_info = localtime(&raw_time);

    // Format time as [HH:MM:SS]
    strftime(buffer, len, "[%H:%M:%S]", time_info);
}

// Function to receive messages from the server
void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    char timestamp[20];

    while (1) {
        // Clear the buffer
        bzero(buffer, BUFFER_SIZE);

        // Receive message from the server
        ssize_t n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0) {
            // If no message received or disconnected
            printf("\nDisconnected from server.\n");
            close(sockfd);
            exit(0);
        }

        buffer[n] = '\0'; // Null terminate the message

        // Lock the mutex to access shared resources safely
        pthread_mutex_lock(&lock);

        // Get the current timestamp
        get_time(timestamp, sizeof(timestamp));

        // Handle different types of messages
        if (strncmp(buffer, "Error:", 6) == 0) {
            // Print error messages without timestamp
            printf("%s\n", buffer);
        } 
        else if (strncmp(buffer, "ACK:", 4) == 0) {
            // Print acknowledgment messages with timestamp
            printf("%s %s\n", timestamp, buffer);
            skip_prompt_once = 0; // Reset flag to allow prompt after ACK
        } 
        else if (strncmp(buffer, "ACTIVE_USERS:", 13) == 0) {
            // Print active users list with timestamp, excluding "ACTIVE_USERS:" prefix
            printf("%s %s", timestamp, buffer + 13);
        } 
        else if (is_typing) {
            // Store the message while the user is typing
            if (msg_count < MAX_BUFFERED_MSGS) {
                snprintf(message_buffer[msg_count++], BUFFER_SIZE, "%s %.*s", timestamp, BUFFER_SIZE - (int)strlen(timestamp) - 2, buffer);
                unread_count++;
            }
        } 
        else {
            // Print incoming messages with timestamp
            printf("\n%s %s\n", timestamp, buffer);
        }

        // If prompt has not been skipped, ask user if they want to send a message
        if (skip_prompt_once == 0) {
            printf("Do you want to send a message? (y): ");
            fflush(stdout);
        }

        pthread_mutex_unlock(&lock); // Unlock the mutex
    }
}

int main(int argc, char *argv[]) {
    // Ensure correct usage
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <your_name>\n", argv[0]);
        exit(1);
    }

    struct sockaddr_in serv_addr;
    pthread_mutex_init(&lock, NULL); // Initialize mutex

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    // Set server address parameters
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2])); // Convert port number to network byte order

    // Convert server IP address to binary form
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0)
        error("Invalid address");

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    // Send the username to the server
    send(sockfd, argv[3], strlen(argv[3]), 0);

    printf("Connected as %s. Use format 'Recipient:Message' to chat. Type 'active' to get the list of online users.\n", argv[3]);

    pthread_t recv_thread;
    // Create thread to receive messages
    if (pthread_create(&recv_thread, NULL, receive_messages, NULL) != 0) {
        error("ERROR creating receive thread");
    }

    char user_input[BUFFER_SIZE];
    char choice;

    while (1) {
        // Prompt user to send a message
        if (!skip_prompt_once) {
            printf("Do you want to send a message? (y): ");
            fflush(stdout);
        }
        skip_prompt_once = 0; // Reset flag

        // Get user input
        choice = getchar();
        getchar(); // Consume newline

        if (choice != 'y' && choice != 'Y') {
            continue; // If the choice is not 'y', continue the loop
        }

        is_typing = 1; // Stop displaying incoming messages while typing
        skip_prompt_once = 1; // Skip the next prompt

        printf("You: ");
        fflush(stdout);

        // Clear user input buffer
        bzero(user_input, BUFFER_SIZE);
        fgets(user_input, BUFFER_SIZE, stdin);
        user_input[strcspn(user_input, "\n")] = 0; // Remove newline

        // Handle special "active" command to get list of active users
        if (strcmp(user_input, "active") == 0) {
            send(sockfd, "GET_ACTIVE_USERS", 16, 0);
        } else {
            // Send regular message
            send(sockfd, user_input, strlen(user_input), 0);
        }
        is_typing = 0; // Resume message display

        // Lock the mutex to handle unread messages
        pthread_mutex_lock(&lock);

        // Print acknowledgment messages first
        for (int i = 0; i < msg_count; i++) {
            if (strncmp(message_buffer[i], "ACK:", 4) == 0) {
                printf("%s\n", message_buffer[i]);
            }
        }

        // If there are unread messages, print them
        if (unread_count > 0) {
            printf("\n==== NEW UNREAD MESSAGES ====\n");
            for (int i = 0; i < msg_count; i++) {
                if (strncmp(message_buffer[i], "ACK:", 4) != 0) {
                    printf("%s\n", message_buffer[i]);
                }
            }
            printf("==== END OF UNREAD MESSAGES ====\n");
            unread_count = 0; // Reset unread message count
        }

        msg_count = 0; // Clear message buffer
        pthread_mutex_unlock(&lock); // Unlock the mutex
    }

    close(sockfd); // Close the socket
    return 0;
}