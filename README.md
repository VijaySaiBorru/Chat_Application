# Client-Server Chat Application (Socket Programming in C)

A multi-client chat system supporting both thread-based and select-based server models, with a terminal and graphical client interface.

---

## Features

- **Multi-client Support**: Handle multiple clients simultaneously.
- **Thread and Select-based Servers**: Choose between thread-based or select-based server for different concurrency models.
- **Direct and Group Messaging**: Send messages to a specific client or broadcast to all active clients.
- **User Management**: Clients can join/disconnect, and the server tracks active users.
- **Inactivity Timeout**: Server disconnects clients after 5 minutes of inactivity.
- **Acknowledgments**: Clients receive confirmation when messages are delivered.
- **Thread Safety**: Uses mutexes to ensure safe access to shared resources.
- **Message Buffering**: Incoming messages are buffered while the user is typing.
- **User Interface**: Optional Raylib-based graphical client for a modern chat experience.

---

## Project Structure

| File Name                    | Description                                |
|-----------------------------|--------------------------------------------|
| `final_server.c`            | Main thread-based server                   |
| `final_server_using_select.c` | Select-based server                      |
| `final_client.c`            | Terminal-based client                      |
| `final_client_ui.c`         | Raylib-based graphical client (optional)   |

---

## How to Build and Run

### Prerequisites

- Unix/Linux OS (recommended)
- GCC compiler
- (Optional) Raylib library for GUI client

### Compilation

```bash
# Thread-based Server
gcc final_server.c -o server -lpthread
./server <port>

# Select-based Server
gcc final_server_using_select.c -o server_select
./server_select <port>

# Terminal Client
gcc final_client.c -o client -lpthread
./client <server_ip> <port> <username>

# Graphical Client (requires Raylib)
gcc final_client_ui.c -o client_ui -lraylib -lpthread
./client_ui <server_ip> <port> <username>
