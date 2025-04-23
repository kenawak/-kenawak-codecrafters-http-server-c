#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctype.h> // Added for tolower in strncasecmp

#define BUFFER_SIZE 4096
#define MAX_PATH 256

// Send HTTP response
void send_response(int client_sock, const char *status) {
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "HTTP/1.1 %s\r\n\r\n", status);
    write(client_sock, response, strlen(response));
}

// Case-insensitive string comparison
int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n-- && *s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
            return *s1 - *s2;
        }
        s1++;
        s2++;
    }
    return 0;
}

// Handle POST /files/{filename}
void handle_post(int client_sock, char *request, const char *directory) {
    // Parse request line
    char *method = strtok(request, " ");
    char *path = strtok(NULL, " ");
    if (!method || !path || strcmp(method, "POST") != 0 || strncmp(path, "/files/", 7) != 0) {
        send_response(client_sock, "404 Not Found");
        return;
    }

    // Extract filename
    char *filename = path + 7;
    if (strlen(filename) == 0) {
        send_response(client_sock, "400 Bad Request");
        return;
    }

    // Parse headers
    int content_length = 0;
    char *content_type = NULL;
    char *body = strstr(request, "\r\n\r\n");
    if (!body) {
        send_response(client_sock, "400 Bad Request");
        return;
    }
    body += 4; // Skip "\r\n\r\n"

    // Read headers
    char *header_end = body - 4;
    *header_end = '\0';
    char *header = strtok(request, "\r\n");
    header = strtok(NULL, "\r\n"); // Skip request line
    while (header) {
        if (strncasecmp(header, "Content-Length: ", 15) == 0) {
            content_length = atoi(header + 15);
        } else if (strncasecmp(header, "Content-Type: ", 13) == 0) {
            content_type = header + 13;
        }
        header = strtok(NULL, "\r\n");
    }

    // Validate Content-Type
    if (!content_type || strncasecmp(content_type, "application/octet-stream", 24) != 0) {
        send_response(client_sock, "400 Bad Request");
        return;
    }

    // Construct filepath
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s%s", directory, filename);
    if (strlen(filepath) >= MAX_PATH) {
        send_response(client_sock, "400 Bad Request");
        return;
    }

    // Create file
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        send_response(client_sock, "500 Internal Server Error");
        return;
    }

    // Write body to file
    if (content_length > 0) {
        // Write initial body part
        size_t initial_body_len = strlen(body);
        if (initial_body_len > 0) {
            write(fd, body, initial_body_len);
        }

        // Read remaining body
        size_t total_written = initial_body_len;
        char buffer[  = {0};
        while (total_written < content_length) {
            int bytes_to_read = BUFFER_SIZE < (content_length - total_written) ? BUFFER_SIZE : (content_length - total_written);
            int bytes_read = read(client_sock, buffer, bytes_to_read);
            if (bytes_read <= 0) {
                close(fd);
                send_response(client_sock, "400 Bad Request");
                return;
            }
            write(fd, buffer, bytes_read);
            total_written += bytes_read;
        }
    }

    close(fd);
    send_response(client_sock, "201 Created");
}

// Main server
int main(int argc, char *argv[]) {
    // Parse --directory flag
    char *directory = "/tmp/";
    if (argc > 2 && strcmp(argv[1], "--directory") == 0) {
        directory = argv[2];
    }

    // Ensure directory ends with '/'
    char *dir_with_slash = directory;
    if (directory[strlen(directory) - 1] != '/') {
        dir_with_slash = malloc(strlen(directory) + 2);
        if (!dir_with_slash) {
            perror("Memory allocation failed");
            return 1;
        }
        sprintf(dir_with_slash, "%s/", directory);
    }

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        free(dir_with_slash == directory ? NULL : dir_with_slash);
        return 1;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_fd);
        free(dir_with_slash == directory ? NULL : dir_with_slash);
        return 1;
    }

    // Configure server address
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(4221)
    };

    // Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        free(dir_with_slash == directory ? NULL : dir_with_slash);
        return 1;
    }

    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        free(dir_with_slash == directory ? NULL : dir_with_slash);
        return 1;
    }

    printf("Server listening on port 4221...\n");

    // Main loop
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        // Read request
        char buffer[BUFFER_SIZE] = {0};
        int bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            close(client_sock);
            continue;
        }
        buffer[bytes_read] = '\0';

        // Handle POST request
        handle_post(client_sock, buffer, dir_with_slash);

        // Close client socket
        close(client_sock);
    }

    // Cleanup
    close(server_fd);
    free(dir_with_slash == directory ? NULL : dir_with_slash);
    return 0;
}