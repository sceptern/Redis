# Redis Server

A Redis-compatible key-value store implemented in C++ from scratch, featuring a custom binary protocol, RESP compatibility layer, high-performance `poll()` and `epoll()` event loops, Pub/Sub messaging, and a built-in vector database for semantic search using sentence-transformers.

---

# Features

- Custom binary protocol on port **1234** for low-overhead clients
- RESP protocol on port **6379** for `redis-cli` and `redis-benchmark` compatibility
- String and Sorted Set (ZSet) data types
- AVL tree + hash table implementation for O(log n) sorted set operations
- Key expiration (TTL) using a min-heap scheduler
- Idle connection timeouts using a doubly linked list
- Thread pool for asynchronous deletion of large data structures
- Both **poll()** and **epoll()** event loop implementations
- Pub/Sub messaging with automatic subscriber cleanup
- Configurable `--max-events` and `--idle-timeout`
- Built-in vector database with semantic search
- Sentence-transformer embeddings served by a persistent Python subprocess
- Cosine similarity search over stored embeddings

---

# Supported Commands

## Key / Value

| Command | Description |
|---------|-------------|
| `GET key` | Get string value |
| `SET key value` | Set string value |
| `DEL key` | Delete key |
| `KEYS` | List all keys |
| `PEXPIRE key ms` | Set TTL in milliseconds |
| `PTTL key` | Get remaining TTL |

---

## Sorted Sets

| Command | Description |
|---------|-------------|
| `ZADD zset score member` | Add/update member |
| `ZREM zset member` | Remove member |
| `ZSCORE zset member` | Get member score |
| `ZQUERY zset score member offset limit` | Range query |

---

## Pub/Sub

| Command | Description |
|---------|-------------|
| `SUBSCRIBE channel [channel ...]` | Subscribe to channels |
| `UNSUBSCRIBE [channel ...]` | Unsubscribe from one or all channels |
| `PUBLISH channel message` | Broadcast a message |

---

## Vector Database

| Command | Description |
|---------|-------------|
| `VSET text` | Embed and store text |
| `VSEARCH text [k]` | Return top-k semantically similar entries |
| `VDEL text` | Delete stored vector |

`VSEARCH` returns a flat array of:

```
[text, similarity_score, text, similarity_score, ...]
```

ordered by cosine similarity.

Example:

```text
VSET "dogs are loyal companions"
VSET "puppies love playing fetch"
VSET "stock prices increased today"

VSEARCH "cute animals" 2

→
[
  "puppies love playing fetch",
  0.72,
  "dogs are loyal companions",
  0.68
]
```

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
> (ACLs, statistics, notifications, etc.). Bare-metal Redis typically exceeds
> 100k ops/sec; WSL2 networking overhead affects both servers.

---

## epoll() vs poll()

100k requests, 1024-byte payload.

| Clients | epoll ops/sec | poll ops/sec | epoll max latency | poll max latency |
|---------:|--------------:|-------------:|------------------:|-----------------:|
| 50 | 67k | 75k | 1.5 ms | 1.9 ms |
| 200 | 60k | 70k | 4.4 ms | 4.4 ms |
| 1000 | 53k | 63k | 16 ms | 14 ms |
| 5000 | 52k | 52k | **76 ms** | **549 ms** |
| 10000 | **59k** | **13k** | **141 ms** | **3000 ms** |

`poll()` scales linearly with the number of file descriptors and collapses
around 10k concurrent connections. `epoll()` maintains stable throughput by
only returning descriptors that are actually ready.

---

## Vector Database Performance

Model:
**all-MiniLM-L6-v2** (384-dimensional embeddings)

| Stored vectors | Avg insert | Avg search (top-5) | p95 search |
|---------------:|-----------:|-------------------:|-----------:|
| 100 | 10.93 ms | 11.55 ms | 13.72 ms |
| 1,000 | 8.84 ms | 15.72 ms | 24.76 ms |
| 10,000 | 8.36 ms | 64.79 ms | 69.19 ms |

Insertion latency is dominated by the embedding generation (~8 ms). The
actual C++ insertion is sub-millisecond.

Search currently performs a brute-force cosine similarity scan over every
stored embedding, resulting in O(n) complexity. An ANN structure such as
HNSW would reduce search time dramatically for larger datasets.

---

# Build

## poll() server

```bash
clang++ -O2 -march=native \
    -o server \
    server.cpp \
    hashtable.cpp \
    avl.cpp \
    zset.cpp \
    heap.cpp \
    thread_pool.cpp \
    -lm
```

## epoll() + Vector Database

```bash
clang++ -O2 -march=native \
    -o server_epoll \
    server_epoll.cpp \
    hashtable.cpp \
    avl.cpp \
    zset.cpp \
    heap.cpp \
    thread_pool.cpp \
    python_worker.cpp \
    vdb.cpp \
    -lm
```

---

# Dependencies

The vector database requires Python 3.

```bash
python3 -m venv venv

source venv/bin/activate

pip install sentence-transformers
```

At startup, the server launches `python_worker.py` as a persistent subprocess
using `fork()` and `exec()`. Communication occurs through stdin/stdout pipes
using newline-delimited JSON.

The virtual environment must be activated before starting the server so
`python3` resolves to the correct interpreter.

---

# Usage

## Start the poll server

```bash
./server
```

---

## Start the epoll server

```bash
source venv/bin/activate

./server_epoll
```

---

## High connection count

```bash
./server_epoll \
    --max-events 10000 \
    --idle-timeout 60000
```

---

## redis-cli

```bash
redis-cli -p 6379 SET foo bar
redis-cli -p 6379 GET foo
```

---

## Binary client

```bash
./client SET foo bar
```

---

## Pub/Sub

```bash
redis-cli -p 6379 SUBSCRIBE news

redis-cli -p 6379 PUBLISH news "hello"
```

---

## Vector Database

```bash
redis-cli -p 6379 VSET "dogs are cute"

redis-cli -p 6379 VSET "cats are adorable"

redis-cli -p 6379 VSEARCH "cute pets" 3

redis-cli -p 6379 VDEL "dogs are cute"
```

---

## Benchmarks

```bash
redis-benchmark \
    -p 6379 \
    -t set,get \
    -n 100000

redis-benchmark \
    -p 6379 \
    -t set,get \
    -n 100000 \
    -c 10000

python3 benchmarkVDB.py
```

---

# Architecture

- Single-threaded event loop using either `poll()` or `epoll()`
- Open-addressing hash table with incremental resizing
- AVL tree + hash table implementation for sorted sets
- Min-heap scheduler for TTL expiration
- Doubly linked list for idle connection management
- Thread pool for asynchronous destruction of large values
- Dual protocol support:
  - Binary protocol on **1234**
  - RESP protocol on **6379**
- Pub/Sub implemented using a global

```cpp
unordered_map<string, unordered_set<Conn*>>
```

Subscribers are automatically removed when clients disconnect.

---

## Vector Database

The vector database is implemented as a separate `VDB` structure.

Each entry stores:

- original text
- 384-dimensional float embedding

Embeddings are indexed by text using the same custom hash table.

Search performs:

1. Embed query text
2. Iterate over every stored embedding
3. Compute cosine similarity
4. Return top-k highest scores

---

## Embedding Service

Rather than loading a transformer model for every request, the server
maintains a persistent Python worker process.

Startup sequence:

1. `fork()`
2. `exec()` `python_worker.py`
3. `dup2()` redirects stdin/stdout to pipes
4. Worker loads the sentence-transformer model
5. Worker signals readiness
6. Requests are exchanged as newline-delimited JSON

The worker shuts down cleanly when the server receives `SIGINT` or
`SIGTERM`.
