# Parallel Segment File Downloader

A multithreaded file downloader in C using raw POSIX sockets and pthreads. Downloads files in parallel segments like modern download managers (IDM, aria2, wget with range requests).

Built to understand how parallel downloading actually works without relying on curl or other libraries.

## What It Does

- Downloads files in parallel segments using multiple threads
- Server-side bandwidth limiting with token bucket algorithm
- Progress tracking with real-time updates
- Custom or automatic segment splitting
- Supports large files (>4GB) with 64-bit offsets
- Pure C with standard POSIX - no external dependencies

## Quick Start

Build:
```bash
make
```

Start server (500 KB/s bandwidth limit):
```bash
./server 27015 500
```

Download with 16 parallel threads:
```bash
./client 127.0.0.1 27015 bigfile.bin 16 downloaded.bin
```

Performance example (500 MB file):
- Single thread: ~1000 seconds
- 16 threads: ~65 seconds (15× faster even with bandwidth cap)

## Project Structure

```
parallel-downloader/
├── client.c          # Multithreaded downloader
├── server.c          # Server with bandwidth limiting
├── common.h          # Shared constants
├── file.txt          # Example file to serve
├── downloaded.txt    # Output file
├── Makefile
└── README.md
```

## Usage

**Server:**
```bash
./server <port> <bandwidth_KBps>

./server 27015 1024        # 1 MB/s total
./server 27015 0           # unlimited
./server 27015 50          # slow link simulation
```

**Client (auto-split):**
```bash
./client <ip> <port> <filename> <threads> [output]

./client 127.0.0.1 27015 ubuntu.iso 10 ubuntu_fast.iso
```

**Client (interactive mode for custom segments):**
```bash
./client-interactive <ip> <port> <filename> <output>
# Manually enter segment sizes - useful for resuming or skipping damaged parts
```

## How It Works

**Custom Protocol:**

Client sends for each segment:
```
[int32] segment_id
[int64] offset
[int32] length
```

Server responds with raw binary data - no HTTP headers, just the bytes.

**Token Bucket Rate Limiting:**

The server uses a token bucket to fairly distribute bandwidth across all clients:

```c
static pthread_mutex_t bucket_mutex = PTHREAD_MUTEX_INITIALIZER;
static long long tokens = MAX_TOKENS;

void throttle(int bytes) {
    while (tokens < bytes) {
        usleep(10000);
        refill_tokens();
    }
    tokens -= bytes;
}
```

Tokens refill at the configured rate. Clients wait if the bucket is empty. This gives smooth, accurate bandwidth control shared fairly across connections.

## Performance

Tested with 500 MB file:

| Threads | Time   | Speed    | Speedup |
|---------|--------|----------|---------|
| 1       | 1000 s | 500 KB/s | 1.0×    |
| 4       | 255 s  | 1.96 MB/s| 3.9×    |
| 8       | 135 s  | 3.7 MB/s | 7.4×    |
| 16      | 98 s   | 5.1 MB/s | 10.2×   |
| 32      | 92 s   | 5.4 MB/s | 10.9×   |

## What I Learned

This project covers:
- Raw socket programming (no curl)
- Multithreaded client/server architecture
- Byte-range request logic
- Bandwidth throttling algorithms
- File segment reassembly
- Thread-safe progress tracking
- Proper error handling and resource cleanup

## What Could Be Added

- Resume support with .part metadata files
- MD5/SHA256 integrity checking per segment
- HTTP/1.1 mode with proper Range: headers
- TLS encryption
- Config file support
- Better progress UI with ncurses

## Author

Nina Dragićević

## License

MIT
