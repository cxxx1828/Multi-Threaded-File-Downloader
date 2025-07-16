#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 27015
#define OUTPUT_FILE "downloaded.txt"

typedef struct {
    int part_id;
    int offset;
    int length;
    char *buffer;
} DownloadPart;

void *download_part(void *arg);

int main() {
    // Otvori lokalno file.txt da saznaš njegovu veličinu
    FILE *fp = fopen("file.txt", "rb");
    if (!fp) {
        perror("Greška pri otvaranju file.txt");
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    fclose(fp);

    printf("Veličina originalne datoteke: %d bajta\n", file_size);

    // Dinamička alokacija za segmente
    DownloadPart *parts = malloc(100 * sizeof(DownloadPart)); // max 100 segmenata
    pthread_t *threads = malloc(100 * sizeof(pthread_t));

    int current_offset = 0;
    int segment_count = 0;

    // Unos dužina segmenata dok ne stignemo do kraja fajla
    while (current_offset < file_size) {
        int seg_len;
        printf("Unesite duzinu za segment %d (preostalo %d bajta): ", segment_count, file_size - current_offset);
        scanf("%d", &seg_len);

        // Ograniči ako korisnik unese prevelik broj
        if (seg_len > file_size - current_offset)
            seg_len = file_size - current_offset;

        parts[segment_count].part_id = segment_count;
        parts[segment_count].offset = current_offset;
        parts[segment_count].length = seg_len;
        parts[segment_count].buffer = malloc(seg_len);

        pthread_create(&threads[segment_count], NULL, download_part, &parts[segment_count]);

        current_offset += seg_len;
        segment_count++;
    }

    // Čekaj sve niti da završe
    for (int i = 0; i < segment_count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Spoji sve u jednu datoteku
    FILE *out = fopen(OUTPUT_FILE, "wb");
    for (int i = 0; i < segment_count; i++) {
        fwrite(parts[i].buffer, 1, parts[i].length, out);
        free(parts[i].buffer);
    }
    fclose(out);

    free(parts);
    free(threads);

    printf("Preuzimanje završeno. Fajl sačuvan kao '%s'\n", OUTPUT_FILE);
    return 0;
}

void *download_part(void *arg) {
    DownloadPart *part = (DownloadPart *)arg;

    int sock;
    struct sockaddr_in server;

    // Kreiranje socket-a
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Ne mogu kreirati socket");
        return NULL;
    }

    // Povezivanje sa serverom
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Neuspješno povezivanje");
        close(sock);
        return NULL;
    }

    // Slanje part_id, offset, length
    send(sock, &part->part_id, sizeof(int), 0);
    send(sock, &part->offset, sizeof(int), 0);
    send(sock, &part->length, sizeof(int), 0);

    // Prijem podataka
    int bytes_received = 0;
    while (bytes_received < part->length) {
        int n = recv(sock, part->buffer + bytes_received, part->length - bytes_received, 0);
        if (n <= 0) break;
        bytes_received += n;
    }

    printf("Segment %d preuzet (%d bajtova)\n", part->part_id, bytes_received);
    close(sock);
    return NULL;
}
