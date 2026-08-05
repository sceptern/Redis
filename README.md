---

# Performance

## Throughput vs Redis 7.0

Benchmarked on **WSL2** against Redis **7.0.15**
(1M requests, 1024-byte payloads, persistence disabled, 50 concurrent clients).

| Metric | This Server | Redis 7.0 |
|---------|------------:|----------:|
| SET throughput | **65,737 req/s** | 59,580 req/s |
| GET throughput | **75,018 req/s** | 65,763 req/s |
| p50 latency | **0.343 ms** | 0.399 ms |
| p99 latency | **0.775 ms** | 0.883 ms |

> The throughput advantage reflects Redis's additional production features
> (ACLs, statistics, notifications). Bare-metal Redis typically exceeds
> 100k ops/sec; WSL2 networking overhead affects both servers equally.

---

## epoll() vs poll()

100k requests, 1024-byte payload.

| Clients | epoll ops/sec | poll ops/sec | epoll max latency | poll max latency |
|---------:|--------------:|-------------:|------------------:|-----------------:|
| 50 | 67k | 75k | 1.5 ms | 1.9 ms |
| 200 | 60k | 70k | 4.4 ms | 4.4 ms |
| 1,000 | 53k | 63k | 16 ms | 14 ms |
| 5,000 | 52k | 52k | **76 ms** | **549 ms** |
| 10,000 | **59k** | **13k** | **141 ms** | **3,000 ms** |

`poll()` scales linearly with file descriptor count and collapses around 10k
concurrent connections. `epoll()` maintains stable throughput via O(1) event
notification.

---

## Vector Database Performance

Model: **all-MiniLM-L6-v2** (384-dimensional embeddings)
Built with: `clang++ -O3 -mavx2 -mfma`, linked against jemalloc

### Search latency — scalar vs AVX2 SIMD

| Stored vectors | Scalar search (top-5) | AVX2 search (top-5) | Speedup |
|---------------:|----------------------:|--------------------:|--------:|
| 100 | 11.55 ms | 10.34 ms | 1.1× |
| 1,000 | 15.72 ms | 8.65 ms | 1.8× |
| 10,000 | 64.79 ms | 13.21 ms | **4.9×** |

The AVX2 implementation processes 8 floats per instruction using `__m256`
intrinsics, reducing the 384-dimension dot product from 384 scalar
multiplications to 48 SIMD operations. The gain compounds at larger corpus
sizes as the scan dominates over the fixed ~8ms embedding round-trip.

### Insert latency

| Stored vectors | Avg insert latency |
|---------------:|-------------------:|
| 100 | 10.49 ms |
| 1,000 | 8.36 ms |
| 10,000 | 8.09 ms |

Insert latency is flat regardless of corpus size — dominated by the Python
embedding round-trip (~8 ms). The C++ HMap insert is sub-millisecond.

Search scales O(n) with corpus size (brute-force scan). ANN indexing (HNSW)
is the natural next step to reduce search to O(log n).

---

## Thread Pool — Event Loop Responsiveness

The thread pool offloads destruction of large data structures so the event
loop never stalls. Benchmarked by firing concurrent GETs during a DEL of a
large ZSet:

| ZSet size | DEL latency | GET avg during DEL | GET max during DEL |
|----------:|------------:|-------------------:|-------------------:|
| 1,000 | 0.289 ms | 0.147 ms | 0.565 ms |
| 10,000 | 0.203 ms | 0.146 ms | 0.742 ms |
| 100,000 | 0.233 ms | 0.143 ms | 0.324 ms |
| 500,000 | 0.211 ms | 0.145 ms | 0.844 ms |

DEL returns in under 0.5ms regardless of ZSet size. 1,379 concurrent GETs
completed during the destruction of a 500k-member ZSet with a max latency
of 0.844ms — the event loop never stalled.

---

## ZSet Throughput

| Members | ZADD avg latency | ZADD throughput |
|--------:|-----------------:|----------------:|
| 1,000 | 0.198 ms | 5,057 ops/sec |
| 10,000 | 0.168 ms | 5,945 ops/sec |
| 100,000 | 0.174 ms | 5,759 ops/sec |
| 500,000 | 0.162 ms | 6,156 ops/sec |

ZADD latency is flat across dataset sizes, confirming O(log n) AVL tree
insert with negligible growth at these scales.

---

# Build

## poll() server

```bash
clang++ -O3 -march=native \
    -o server \
    server.cpp \
    hashtable.cpp \
    avl.cpp \
    zset.cpp \
    heap.cpp \
    thread_pool.cpp \
    -lm -ljemalloc
```

## epoll() + Vector Database

```bash
clang++ -O3 -mavx2 -mfma \
    -o server_epoll \
    server_epoll.cpp \
    hashtable.cpp \
    avl.cpp \
    zset.cpp \
    heap.cpp \
    thread_pool.cpp \
    python_worker.cpp \
    vdb.cpp \
    -lm -ljemalloc
```

---

# Dependencies

The vector database requires Python 3 with sentence-transformers:

```bash
python3 -m venv venv
source venv/bin/activate
pip install sentence-transformers
```

jemalloc (optional, recommended):

```bash
sudo apt install libjemalloc-dev
```

At startup, the server launches `python_worker.py` as a persistent subprocess
using `fork()` and `exec()`. Communication occurs over stdin/stdout pipes
using newline-delimited JSON. The venv must be active when starting the server
so `python3` resolves to the correct interpreter.

---

# Usage

## Start the epoll server

```bash
source venv/bin/activate
./server_epoll
```

## High connection count

```bash
./server_epoll --max-events 10000 --idle-timeout 60000
```

## redis-cli

```bash
redis-cli -p 6379 SET foo bar
redis-cli -p 6379 GET foo
```

## Pub/Sub

```bash
redis-cli -p 6379 SUBSCRIBE news
redis-cli -p 6379 PUBLISH news "hello"
```

## Vector Database

```bash
redis-cli -p 6379 VSET "dogs are cute"
redis-cli -p 6379 VSET "cats are adorable"
redis-cli -p 6379 VSEARCH "cute pets" 3
redis-cli -p 6379 VDEL "dogs are cute"
```

## Benchmarks

```bash
# GET/SET throughput
redis-benchmark -p 6379 -t set,get -n 100000

# High connection count
redis-benchmark -p 6379 -t set,get -n 100000 -c 10000

# Vector database
python3 benchmarkVDB.py

# ZSet + thread pool responsiveness
python3 benchmarkZSet.py
```

---

# Architecture

- Single-threaded event loop using either `poll()` or `epoll()`
- Open-addressing hash table with incremental resizing
- AVL tree + hash table for O(log n) sorted set operations
- Min-heap scheduler for TTL expiration
- Doubly linked list for idle connection timeout tracking
- Thread pool for asynchronous destruction of large values (ZSets > 1k members)
- Dual protocol: binary on port **1234**, RESP on port **6379**
- Pub/Sub via `unordered_map<string, unordered_set<Conn*>>` — subscribers
  removed automatically on disconnect via `conn_destroy`

## Vector Database

Separate `VDB` struct with its own `HMap` keyed by text. Each `VEntry` stores
a 384-dimensional float embedding. `VSEARCH` scans all entries via `hm_foreach`
and ranks by cosine similarity.

Cosine similarity is computed using AVX2 SIMD intrinsics (`__m256`), processing
8 floats per instruction. At 10k vectors this reduces search latency from 64ms
(scalar) to 13ms (AVX2) — a 4.9× improvement.

## Embedding Service

The server maintains a persistent Python worker process to avoid model reload
overhead on every request.

Startup sequence:
1. `fork()` duplicates the server process
2. `dup2()` rewires the child's stdin/stdout to pipes
3. `exec()` replaces the child with `python_worker.py`
4. Worker loads the sentence-transformer model (~1s)
5. Worker sends `{"status": "ready"}` — server blocks until this arrives
6. Requests exchanged as newline-delimited JSON for the lifetime of the server

Worker shuts down cleanly on `SIGINT`/`SIGTERM` via `waitpid()`.
