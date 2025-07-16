#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 27015
#define FILENAME "file.txt"
#define BUF_SIZE 1024
#define SPEED_LIMIT_KBPS 10  // ograničenje ~10 KB/s

void *handle_client(void *arg);

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server, client;
    socklen_t c = sizeof(struct sockaddr_in);

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Ne mogu kreirati socket");
        return 1;
    }

    // Setup server info
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    // Bind
    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Greška pri bindovanju");
        return 1;
    }
    puts("Bind uspješan. Čekam konekcije...");

    listen(server_sock, 5);

    while ((client_sock = accept(server_sock, (struct sockaddr *)&client, &c))) {
        printf("Povezivanje prihvaćeno\n");

        pthread_t thread;
        int *sock = malloc(sizeof(int));
        *sock = client_sock;

        pthread_create(&thread, NULL, handle_client, sock);
        pthread_detach(thread);
    }

    close(server_sock);
    return 0;
}

void *handle_client(void *arg) {
    int sock = *(int *)arg;
    free(arg);

    FILE *fp = fopen(FILENAME, "rb");
    if (!fp) {
        perror("Greška pri otvaranju fajla");
        close(sock);
        return NULL;
    }

    int segment_id, offset, length;
    char buffer[BUF_SIZE];

    // Primanje segment_id, offset i length
    recv(sock, &segment_id, sizeof(int), 0);
    recv(sock, &offset, sizeof(int), 0);
    recv(sock, &length, sizeof(int), 0);

    printf("Zahtev za segment %d (offset: %d, length: %d)\n", segment_id, offset, length);

    fseek(fp, offset, SEEK_SET);

    int bytes_sent = 0;
    while (bytes_sent < length) {
        int to_read = (length - bytes_sent > BUF_SIZE) ? BUF_SIZE : (length - bytes_sent);
        fread(buffer, 1, to_read, fp);

        send(sock, buffer, to_read, 0);
        bytes_sent += to_read;

        // Ograničenje brzine
        usleep(1000000 * to_read / (SPEED_LIMIT_KBPS * 1024));
    }

    fclose(fp);
    close(sock);
    return NULL;
}
