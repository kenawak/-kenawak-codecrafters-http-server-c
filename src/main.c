#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

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
            char response[1024];
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
            } else {
                // Handle invalid paths
                snprintf(response, sizeof(response),
                         "HTTP/1.1 404 Not Found\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n");
            }

            // Send response
            if (write(client_fd, response, strlen(response)) < 0) {
                printf("Write failed: %s\n", strerror(errno));
            }

            // Close client and exit child
            close(client_fd);
            exit(0);
        } else {
            // Parent process: Close client socket and clean up zombies
            close(client_fd);
            // Non-blocking wait to prevent zombie processes
            while (waitpid(-1, NULL, WNOHANG) > 0);
        }
    }

    close(server_fd);
    return 0;
}