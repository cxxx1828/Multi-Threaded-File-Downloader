#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 27015
#define FILENAME "file.txt"
#define MAX_CLIENTS 50
#define SPEED_LIMIT_KBPS 50    // Simulated bandwidth per client (~50 KB/s)
#define CHUNK_SIZE 8192

void *handle_client(void *arg);

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server, client;
    socklen_t client_len = sizeof(client);

    // Open file once and keep size
    struct stat st;
    if (stat(FILENAME, &st) != 0) {
        perror("Cannot access file.txt");
        return 1;
    }
    long file_size = st.st_size;
    printf("Serving '%s' (%ld bytes) on port %d\n", FILENAME, file_size, PORT);
    printf("Speed limited to ~%d KB/s per client\n", SPEED_LIMIT_KBPS);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        return 1;
    }

    printf("Server listening on 0.0.0.0:%d...\n\n", PORT);

    while ((client_sock = accept(server_sock, (struct sockaddr *)&client, &client_len))) {
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("New connection from %s:%d\n", client_ip, ntohs(client.sin_port));

        pthread_t thread;
        int *sock_ptr = malloc(sizeof(int));
        *sock_ptr = client_sock;

        if (pthread_create(&thread, NULL, handle_client, sock_ptr) != 0) {
            fprintf(stderr, "Failed to create thread\n");
            close(client_sock);
            free(sock_ptr);
        } else {
            pthread_detach(thread);  // Auto-cleanup when thread ends
        }
    }

    close(server_sock);
    return 0;
}

void *handle_client(void *arg) {
    int sock = *(int *)arg;
    free(arg);

    FILE *fp = fopen(FILENAME, "rb");
    if (!fp) {
        perror("Server: Cannot open file");
        close(sock);
        return NULL;
    }

    int segment_id;
    long offset;
    int length;

    // Receive request: segment_id (int), offset (long), length (int)
    if (recv(sock, &segment_id, sizeof(int), 0) <= 0 ||
        recv(sock, &offset, sizeof(long), 0) <= 0 ||
        recv(sock, &length, sizeof(int), 0) <= 0) {
        fprintf(stderr, "Invalid request from client\n");
        fclose(fp);
        close(sock);
        return NULL;
    }

    printf("Serving segment %d | offset: %ld | length: %d bytes\n", segment_id, offset, length);

    if (fseek(fp, offset, SEEK_SET) != 0) {
        fprintf(stderr, "Seek failed (offset %ld)\n", offset);
        fclose(fp);
        close(sock);
        return NULL;
    }

    char *buffer = malloc(CHUNK_SIZE);
    if (!buffer) {
        fclose(fp);
        close(sock);
        return NULL;
    }

    int bytes_sent = 0;
    while (bytes_sent < length) {
        int to_send = (length - bytes_sent > CHUNK_SIZE) ? CHUNK_SIZE : (length - bytes_sent);
        size_t read = fread(buffer, 1, to_send, fp);

        if (read <= 0) {
            fprintf(stderr, "File read error at offset %ld\n", offset + bytes_sent);
            break;
        }

        int sent = 0;
        while (sent < read) {
            int n = send(sock, buffer + sent, read - sent, 0);
            if (n <= 0) {
                fprintf(stderr, "Send failed\n");
                goto cleanup;
            }
            sent += n;
        }

        bytes_sent += read;

        // Simulate bandwidth throttling
        if (SPEED_LIMIT_KBPS > 0) {
            usleep((read * 1000000LL) / (SPEED_LIMIT_KBPS * 1024));
        }
    }

    printf("Completed segment %d (%d bytes sent)\n", segment_id, bytes_sent);

cleanup:
    free(buffer);
    fclose(fp);
    close(sock);
    return NULL;
}
