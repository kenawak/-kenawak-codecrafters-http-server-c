#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // Get file directory from command-line argument
    const char *file_dir = (argc > 1) ? argv[1] : ".";
    printf("Using file directory: %s\n", file_dir);

    printf("Logs from your program will appear here!\n");

    // Create socket
    int server_fd, client_addr_len;
    struct sockaddr_in client_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Set SO_REUSEADDR
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("SO_REUSEADDR failed: %s \n", strerror(errno));
        return 1;
    }

    // Configure server address
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = { htonl(INADDR_ANY) }
    };

    // Bind socket
    if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
        printf("Bind failed: %s \n", strerror(errno));
        return 1;
    }

    // Listen
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        return 1;
    }

    printf("Waiting for a client to connect...\n");

    while (1) {
        // Accept client
        client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
        if (client_fd < 0) {
            printf("Accept failed: %s\n", strerror(errno));
            continue;
        }

        // Fork to handle client
        pid_t pid = fork();
        if (pid < 0) {
            printf("Fork failed: %s\n", strerror(errno));
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            // Child process: Handle client
            close(server_fd); // Child doesn't need server socket

            printf("Client connected (PID: %d)\n", getpid());

            // Read request
            char buffer[1024] = {0};
            if (read(client_fd, buffer, sizeof(buffer) - 1) < 0) {
                printf("Read failed: %s\n", strerror(errno));
                close(client_fd);
                exit(1);
            }

            // Parse request line
            char method[16], path[256], protocol[16];
            if (sscanf(buffer, "%s %s %s", method, path, protocol) != 3) {
                printf("Failed to parse request line\n");
                close(client_fd);
                exit(1);
            }

            printf("Parsed request: Method=%s, Path=%s, Protocol=%s\n", method, path, protocol);

            // Parse headers to find User-Agent
            char user_agent[256] = {0};
            char *header_line = buffer;
            while (header_line) {
                header_line = strstr(header_line, "\r\n");
                if (!header_line) break;
                header_line += 2; // Skip \r\n
                if (strncmp(header_line, "User-Agent: ", 12) == 0) {
                    strncpy(user_agent, header_line + 12, sizeof(user_agent) - 1);
                    // Remove trailing \r\n
                    char *newline = strstr(user_agent, "\r\n");
                    if (newline) *newline = '\0';
                    break;
                }
            }

            // Prepare response
            char response[4096] = {0};
            if (strcmp(path, "/") == 0) {
                // Handle root path
                snprintf(response, sizeof(response),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n");
            } else if (strncmp(path, "/echo/", 6) == 0) {
                // Handle /echo/{str}
                char *echo_string = path + 6;
                snprintf(response, sizeof(response),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %ld\r\n"
                         "\r\n"
                         "%s",
                         strlen(echo_string), echo_string);
            } else if (strcmp(path, "/user-agent") == 0) {
                // Handle /user-agent
                if (user_agent[0] == '\0') {
                    snprintf(response, sizeof(response),
                             "HTTP/1.1 400 Bad Request\r\n"
                             "Content-Length: 0\r\n"
                             "\r\n");
                } else {
                    snprintf(response, sizeof(response),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain\r\n"
                             "Content-Length: %ld\r\n"
                             "\r\n"
                             "%s",
                             strlen(user_agent), user_agent);
                }
            } else if (strncmp(path, "/files/", 7) == 0) {
                // Handle /files/{filename}
                char *filename = path + 7; // Skip "/files/"
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", file_dir, filename);
                printf("Attempting to open file: %s\n", filepath);

                // Open file
                int file_fd = open(filepath, O_RDONLY);
                if (file_fd < 0) {
                    printf("File open failed: %s\n", strerror(errno));
                    snprintf(response, sizeof(response),
                             "HTTP/1.1 404 Not Found\r\n"
                             "Content-Length: 0\r\n"
                             "\r\n");
                } else {
                    // Get file size
                    struct stat file_stat;
                    if (fstat(file_fd, &file_stat) < 0) {
                        printf("fstat failed: %s\n", strerror(errno));
                        close(file_fd);
                        snprintf(response, sizeof(response),
                                 "HTTP/1.1 500 Internal Server Error\r\n"
                                 "Content-Length: 0\r\n"
                                 "\r\n");
                    } else {
                        off_t file_size = file_stat.st_size;
                        if (file_size == 0) {
                            // Handle empty file
                            snprintf(response, sizeof(response),
                                     "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/octet-stream\r\n"
                                     "Content-Length: 0\r\n"
                                     "\r\n");
                            close(file_fd);
                        } else {
                            // Prepare headers
                            snprintf(response, sizeof(response),
                                     "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/octet-stream\r\n"
                                     "Content-Length: %ld\r\n"
                                     "\r\n",
                                     file_size);
                            // Send headers
                            if (write(client_fd, response, strlen(response)) < 0) {
                                printf("Write headers failed: %s\n", strerror(errno));
                                close(file_fd);
                                exit(1);
                            }
                            // Send file contents in chunks
                            char chunk[4096];
                            ssize_t bytes_read;
                            while ((bytes_read = read(file_fd, chunk, sizeof(chunk))) > 0) {
                                if (write(client_fd, chunk, bytes_read) < 0) {
                                    printf("Write file content failed: %s\n", strerror(errno));
                                    close(file_fd);
                                    exit(1);
                                }
                            }
                            if (bytes_read < 0) {
                                printf("Read file failed: %s\n", strerror(errno));
                                close(file_fd);
                                snprintf(response, sizeof(response),
                                         "HTTP/1.1 500 Internal Server Error\r\n"
                                         "Content-Length: 0\r\n"
                                         "\r\n");
                            } else {
                                response[0] = '\0'; // Clear response to skip final write
                            }
                            close(file_fd);
                        }
                    }
                }
            } else {
                // Handle invalid paths
                snprintf(response, sizeof(response),
                         "HTTP/1.1 404 Not Found\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n");
            }

            // Send response (if not already sent for /files/)
            if (response[0] != '\0') {
                if (write(client_fd, response, strlen(response)) < 0) {
                    printf("Write failed: %s\n", strerror(errno));
                }
            }

            // Close client and exit child
            close(client_fd);
            exit(0);
        } else {
            // Parent process: Close client socket and clean up zombies
            close(client_fd);
            while (waitpid(-1, NULL, WNOHANG) > 0);
        }
    }

    close(server_fd);
    return 0;
}