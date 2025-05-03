#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "raylib.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define BUFFER_SIZE 1024
#define MAX_MESSAGES 20

typedef struct {
    char text[BUFFER_SIZE];
    int is_sent; // 1 if sent, 0 if received
} Message;

// Networking variables
int sockfd;
pthread_mutex_t lock;
Message chat_log[MAX_MESSAGES];
int message_count = 0;

// User Input
char input_text[BUFFER_SIZE] = "";
int input_length = 0;
bool typing = false;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void send_message() {
    if (input_length == 0) return;

    // Check if the message is "active"
    if (strcmp(input_text, "active") == 0) {
        send(sockfd, "GET_ACTIVE_USERS", strlen("GET_ACTIVE_USERS"), 0);
        input_text[0] = '\0'; // Reset the input after sending
        input_length = 0;
        return;
    }

    // Regular message sending logic
    send(sockfd, input_text, strlen(input_text), 0);
    pthread_mutex_lock(&lock);
    if (message_count < MAX_MESSAGES) {
        strcpy(chat_log[message_count].text, input_text);
        chat_log[message_count].is_sent = 1; // Sent
        message_count++;
    } else {
        for (int i = 1; i < MAX_MESSAGES; i++) {
            chat_log[i - 1] = chat_log[i];
        }
        strcpy(chat_log[MAX_MESSAGES - 1].text, input_text);
        chat_log[MAX_MESSAGES - 1].is_sent = 1; // Sent
    }
    pthread_mutex_unlock(&lock);

    input_text[0] = '\0';
    input_length = 0;
}

void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    while (1) {
        bzero(buffer, BUFFER_SIZE);
        ssize_t n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0) {
            printf("Disconnected from server.\n");
            close(sockfd);
            exit(0);
        }
        buffer[n] = '\0';

        // If the message is a list of active users
        if (strstr(buffer, "active clients:") != NULL) {
            printf("Active clients: %s\n", buffer); // Display active clients in the console
        } else {
            // Filter out acknowledgment messages
            if (strstr(buffer, "Message delivered successfully.") == NULL && strstr(buffer, "Message delivered to group.") == NULL) {
                pthread_mutex_lock(&lock);
                if (message_count < MAX_MESSAGES) {
                    strcpy(chat_log[message_count].text, buffer);
                    chat_log[message_count].is_sent = 0; // Received
                    message_count++;
                } else {
                    for (int i = 1; i < MAX_MESSAGES; i++) {
                        chat_log[i - 1] = chat_log[i];
                    }
                    strcpy(chat_log[MAX_MESSAGES - 1].text, buffer);
                    chat_log[MAX_MESSAGES - 1].is_sent = 0; // Received
                }
                pthread_mutex_unlock(&lock);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <your_name>\n", argv[0]);
        exit(1);
    }
    struct sockaddr_in serv_addr;
    pthread_mutex_init(&lock, NULL);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0)
        error("Invalid address");
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    send(sockfd, argv[3], strlen(argv[3]), 0);
    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive_messages, NULL) != 0) {
        error("ERROR creating receive thread");
    }

    char window_title[BUFFER_SIZE];
    snprintf(window_title, BUFFER_SIZE, "%s - ChatApp", argv[3]);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, window_title);

    SetTargetFPS(60);
    Rectangle textBox = {20, 550, 600, 30};
    Rectangle sendButton = {650, 550, 120, 30};

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ENTER)) {
            send_message();
        }

        if (CheckCollisionPointRec(GetMousePosition(), sendButton) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            send_message();
        }
        
        BeginDrawing();
        ClearBackground(RAYWHITE);

        int y_offset = 20;
        pthread_mutex_lock(&lock);
        for (int i = 0; i < message_count; i++) {
            if (chat_log[i].is_sent) {
                int textWidth = MeasureText(chat_log[i].text, 20);
                DrawText(chat_log[i].text, SCREEN_WIDTH - textWidth - 20, y_offset, 20, BLUE);
            } else {
                DrawText(chat_log[i].text, 20, y_offset, 20, DARKGRAY);
            }
            y_offset += 25;
        }
        pthread_mutex_unlock(&lock);

        DrawRectangleRec(textBox, LIGHTGRAY);
        DrawText(input_text, textBox.x + 5, textBox.y + 5, 20, BLACK);
        DrawRectangleRec(sendButton, BLUE);
        DrawText("SEND", sendButton.x + 30, sendButton.y + 5, 20, WHITE);
        EndDrawing();

        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 125 && input_length < BUFFER_SIZE - 1) {
                input_text[input_length++] = (char)key;
                input_text[input_length] = '\0';
            }
            key = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && input_length > 0) {
            input_text[--input_length] = '\0';
        }
    }

    CloseWindow();
    close(sockfd);
    return 0;
}
