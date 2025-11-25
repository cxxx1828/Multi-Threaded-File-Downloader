# Parallel Segment File Downloader – Multithreaded TCP Client & Server in C

A high-performance, educational implementation of **range-based parallel file downloading** using raw POSIX sockets and pthreads – similar to how HTTP/2 or modern download managers (like aria2 or wget with -c) work under the hood.

Perfect for learning:
- Socket programming
- Multithreaded client/server architecture
- Byte-range requests (like HTTP Range header)
- Bandwidth throttling
- File reassembly from segments


## Features

- **Client**: Downloads any file in **N parallel segments** (user-defined sizes or count)
- **Server**: Serves multiple clients concurrently using **pthreads**
- Supports **byte-range requests** (`Range: start-end`)
- Configurable **server-side bandwidth limit** (e.g., 500 KB/s total)
- Precise **rate limiting** using token bucket algorithm
- Robust error handling and graceful shutdown
- Reassembles segments in correct order → perfect output file
- Cross-platform (Linux/macOS, Windows via WSL)

## Demo

```bash
# Start server (serves files from ./server_files/)
./server 8080 512   # port 8080, 512 KB/s limit

# In another terminal - download bigfile.bin in 8 parallel segments
./client 127.0.0.1 8080 bigfile.bin 8 output.bin
```

Result: ~8× faster than single-threaded on high-latency links!

## Project Structure

```
.
├── client.c              → Parallel downloader
├── server.c              → Multithreaded server with bandwidth limit
├── common.h              → Shared structures and constants
├── Makefile
├── server_files/         → Put files here to serve
├── downloads/            → Client saves output here (auto-created)
└── README.md
```

## How It Works

### Protocol (Custom, Simple Text-Based)

Client sends:
```
GET filename HTTP/1.0\r\n
Range: bytes=0-1048575\r\n
\r\n
```

Server replies:
```
OK 1048576\r\n           ← total file size
2000000\r\n               ← segment data length (if partial)
\r\n
[binary data...]
```

### Bandwidth Limiting (Server-Side)

Uses a **global token bucket** protected by mutex:
- Refilled at configured rate (e.g., 512000 bytes/sec)
- Each thread waits if no tokens available
- Smooth, fair sharing between all clients

## Build

```bash
make          # builds both client and server
make clean    # remove binaries
```

Requirements: GCC, make, pthread

## Usage

### Server
```bash
./server <port> <bandwidth_KBps>
./server 8080 1024        # 1 MB/s limit
./server 9000 0           # unlimited speed
```

### Client

**Option 1**: Equal segments by count
```bash
./client <ip> <port> <filename> <threads> [output]
./client 127.0.0.1 8080 ubuntu.iso 10 ubuntu_fast.iso
```

**Option 2**: Custom segment sizes (advanced)
```bash
./client-custom <ip> <port> <filename> output.bin
# Then input ranges interactively:
# 0-10MB
# 10MB-25MB
# 50MB-60MB
# ...
```

## Performance Example

| Threads | Time (100MB file, 512 KB/s limit) |
|--------|-----------------------------------|
| 1      | ~195 seconds                      |
| 4      | ~52 seconds                       |
| 8      | ~28 seconds                       |
| 16     | ~18 seconds (limited by server BW)|


## Future Ideas

- Add resume capability (check existing segments)
- HTTP/1.1 compliant server
- TLS support
- Progress bar with libncurses
- Peer-to-peer extension

## License

**MIT License** – Free to use in education, assignments, or personal projects.

```
Feel free to fork, improve, and submit pull requests!
```

## Author
Nina Dragićević
