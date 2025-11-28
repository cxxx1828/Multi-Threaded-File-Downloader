#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define PORT 27015
#define OUTPUT_FILE "downloaded.txt"
#define MAX_PARTS 100
#define CONNECT_TIMEOUT_SEC 5

typedef struct {
    int part_id;
    long offset;      // Use long for large files (>2GB)
    int length;
    char *buffer;
    int success;      // Flag to indicate successful download
    pthread_mutex_t *mutex;
    int *completed_count;
    int total_parts;
} DownloadPart;

// Function prototype
void *download_part(void *arg);

int main() {
    // Get original file size
    struct stat st;
    if (stat("file.txt", &st) != 0) {
        perror("Error: Cannot stat file.txt (needed to know size)");
        return 1;
    }
    long file_size = st.st_size;
    printf("Original file size: %ld bytes\n", file_size);

    if (file_size == 0) {
        printf("File is empty. Creating empty output.\n");
        FILE *out = fopen(OUTPUT_FILE, "wb");
        fclose(out);
        return 0;
    }

    // Dynamic allocation for parts and threads
    DownloadPart *parts = calloc(MAX_PARTS, sizeof(DownloadPart));
    pthread_t *threads = malloc(MAX_PARTS * sizeof(pthread_t));
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int completed_count = 0;

    long current_offset = 0;
    int segment_count = 0;

    printf("Enter segment sizes (in bytes). Enter 0 or negative to auto-split remaining equally.\n");

    while (current_offset < file_size) {
        if (segment_count >= MAX_PARTS) {
            fprintf(stderr, "Error: Too many segments (max %d)\n", MAX_PARTS);
            break;
        }

        int seg_len;
        printf("Segment %2d (offset: %ld, remaining: %ld bytes): ", 
               segment_count, current_offset, file_size - current_offset);
        if (scanf("%d", &seg_len) != 1 || seg_len <= 0) {
            // Auto-split remaining data equally among remaining slots
            int remaining_slots = MAX_PARTS - segment_count;
            seg_len = (file_size - current_offset) / remaining_slots + 
                     ((file_size - current_offset) % remaining_slots != 0);
            printf("Auto-splitting: using %d bytes per remaining segment\n", seg_len);
        }

        if (seg_len > file_size - current_offset)
            seg_len = file_size - current_offset;

        parts[segment_count] = (DownloadPart){
            .part_id = segment_count,
            .offset = current_offset,
            .length = seg_len,
            .buffer = malloc(seg_len),
            .success = 0,
            .mutex = &mutex,
            .completed_count = &completed_count,
            .total_parts = 0  // Will be set later
        };

        if (!parts[segment_count].buffer) {
            perror("Failed to allocate buffer");
            break;
        }

        current_offset += seg_len;
        segment_count++;
    }

    // Set total parts for progress tracking
    for (int i = 0; i < segment_count; i++) {
        parts[i].total_parts = segment_count;
    }

    printf("Starting download of %d segments using %d threads...\n", segment_count, segment_count);

    // Launch all download threads
    for (int i = 0; i < segment_count; i++) {
        if (pthread_create(&threads[i], NULL, download_part, &parts[i]) != 0) {
            fprintf(stderr, "Failed to create thread for part %d\n", i);
            parts[i].success = 0;
        }
    }

    // Wait for all threads
    for (int i = 0; i < segment_count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Check if all parts succeeded
    int failed = 0;
    for (int i = 0; i < segment_count; i++) {
        if (!parts[i].success) {
            fprintf(stderr, "Part %d failed to download!\n", i);
            failed = 1;
        }
    }

    if (failed) {
        printf("Download incomplete due to errors.\n");
    } else {
        // Write combined file
        FILE *out = fopen(OUTPUT_FILE, "wb");
        if (!out) {
            perror("Cannot create output file");
            return 1;
        }

        for (int i = 0; i < segment_count; i++) {
            fwrite(parts[i].buffer, 1, parts[i].length, out);
        }
        fclose(out);
        printf("Download completed successfully! Saved as '%s' (%ld bytes)\n", OUTPUT_FILE, file_size);
    }

    // Cleanup
    for (int i = 0; i < segment_count; i++) free(parts[i].buffer);
    free(parts);
    free(threads);
    pthread_mutex_destroy(&mutex);

    return failed ? 1 : 0;
}

void *download_part(void *arg) {
    DownloadPart *part = (DownloadPart *)arg;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        part->success = 0;
        return NULL;
    }

    // Set connection timeout
    struct timeval timeout = { .tv_sec = CONNECT_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT)
    };
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        fprintf(stderr, "Part %d: Connection failed (%s)\n", part->part_id, strerror(errno));
        close(sock);
        part->success = 0;
        return NULL;
    }

    // Send request: part_id (for logging), offset, length
    send(sock, &part->part_id, sizeof(int), 0);
    send(sock, &part->offset, sizeof(long), 0);  // Note: using long
    send(sock, &part->length, sizeof(int), 0);

    // Receive data
    int bytes_received = 0;
    while (bytes_received < part->length) {
        int n = recv(sock, part->buffer + bytes_received, part->length - bytes_received, 0);
        if (n <= 0) {
            if (n < 0 && errno == EAGAIN) {
                fprintf(stderr, "Part %d: Receive timeout\n", part->part_id);
            } else if (n == 0) {
                fprintf(stderr, "Part %d: Server closed connection early\n", part->part_id);
            }
            close(sock);
            part->success = 0;
            return NULL;
        }
        bytes_received += n;
    }

    close(sock);
    part->success = 1;

    // Progress update (thread-safe)
    pthread_mutex_lock(part->mutex);
    (*part->completed_count)++;
    printf("[%.1f%%] Part %d downloaded (%d bytes) | %d/%d completed\n",
           100.0 * (*part->completed_count) / part->total_parts,
           part->part_id, bytes_received, *part->completed_count, part->total_parts);
    pthread_mutex_unlock(part->mutex);

    return NULL;
}
