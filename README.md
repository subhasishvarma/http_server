# High-Performance C HTTP Server

A production-grade, non-blocking HTTP/1.1 server engineered from scratch in C. 

## Performance Benchmarks
Tested with ApacheBench (10,000 requests, 100 concurrency):

|        Metric      |     Result    |
|--------------------|---------------|
| **Max Throughput** | 18,606.49 RPS |
| **Mean Latency**   |    5.3 ms     |
| **Failed Requests**|     0         |

## Technical Highlights
* **Architecture:** Non-blocking I/O multiplexing via `poll()`.
* **I/O Optimization:** Zero-copy `sendfile()` integration for static asset delivery.
* **Memory Management:** Custom connection state machine with heap-allocated buffers.
* **Observability:** Asynchronous access logging with sub-millisecond overhead.
* **Security:** Robust request filtering (404/405 status code handling).

## How to Build and Run
1. **Compile:** `gcc src/server.c src/connection.c src/http_parser.c src/response.c src/util.c -o server`
2. **Run:** `./server`