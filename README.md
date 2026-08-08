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
- Document chunking — group named chunks under a document key for scoped, per-document retrieval
- Sentence-transformer embeddings served by a persistent Python subprocess
- AVX2 SIMD-accelerated cosine similarity (8 floats/instruction via `__m256` intrinsics)

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
| `VSET text` | Embed and store a standalone piece of text |
| `VADD doc_name chunk_name text` | Embed and store a named chunk under a document. Creates the document on first use; if `chunk_name` already exists under `doc_name`, its text and embedding are overwritten in place (upsert) |
| `VSEARCH text [k]` | Search only standalone `VSET` entries. Returns top-k by cosine similarity (default k=1) |
| `VSEARCH doc_name text [k]` | Search only the chunks belonging to `doc_name`. Returns top-k chunks from that document |
| `VDEL key` | Delete a `VSET` entry, or an entire document (`VADD`) along with all of its chunks |

A key created with `VSET` and a document created with `VADD` occupy the same namespace but are distinct types — attempting to `VSET` a key that already exists as a document (or vice versa) returns an error rather than silently overwriting it.

`VSEARCH text [k]` (2–3 args) returns a flat array of `[text, score, text, score, ...]`, searching only standalone `VSET` entries — it never returns document chunks.

`VSEARCH doc_name text [k]` (3–4 args) returns a flat array of `[chunk_name, text, score, chunk_name, text, score, ...]`, scoped to a single document's chunks.

Examples:

```
VSET "dogs are loyal companions"
VSET "puppies love playing fetch"
VSET "stock prices increased today"

VSEARCH "cute animals" 2
→ ["puppies love playing fetch", 0.72, "dogs are loyal companions", 0.68]
```

```
VADD animal_facts.pdf intro "this document is about domestic animals"
VADD animal_facts.pdf dogs "dogs are loyal companions and easy to train"
VADD animal_facts.pdf cats "cats are independent and low maintenance"

VSEARCH animal_facts.pdf "loyal pets" 2
→ ["dogs", "dogs are loyal companions and easy to train", 0.81, "intro", "this document is about domestic animals", 0.52]

VDEL animal_facts.pdf
→ 1   (removes the document and all of its chunks)
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

> Note: document (`VADD`) deletion is not yet routed through the thread pool
> the way large ZSet deletion is (see below) — `VDEL` on a document currently
> frees all of its chunks synchronously on the event loop thread. This is a
> non-issue at typical chunk counts per document, but is a candidate for the
> same async-destruction treatment if document sizes grow large.

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
    persist.cpp
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
    persist.cpp
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

Testing the vector database over RESP additionally requires `redis-py`:

```bash
pip install redis
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
# standalone facts
redis-cli -p 6379 VSET "dogs are cute"
redis-cli -p 6379 VSET "cats are adorable"
redis-cli -p 6379 VSEARCH "cute pets" 3
redis-cli -p 6379 VDEL "dogs are cute"

# documents made of named chunks
redis-cli -p 6379 VADD animal_facts.pdf intro "an overview of domestic animals"
redis-cli -p 6379 VADD animal_facts.pdf dogs "dogs are loyal companions"
redis-cli -p 6379 VSEARCH animal_facts.pdf "loyal pets" 2
redis-cli -p 6379 VDEL animal_facts.pdf
```

Note: when using `redis-py` against the RESP port, explicitly pass `protocol=2` when constructing the client (`redis.Redis(..., protocol=2)`). The server always replies to `HELLO` with a flat RESP2 array; a client that negotiates RESP3 will fail to parse it.

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

## Tests

```bash
python3 testVDB.py --port 6379
```

Covers `VSET`/`VADD`/`VDEL`/`VSEARCH` (global and document-scoped), upsert
behavior on repeated `VADD` calls, type conflicts between `VSET` and `VADD`
keys, cascading deletion of a document's chunks, `k`-limit enforcement, and
argument validation.

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

Separate `VDB` struct with its own `HMap` keyed by name. Each `VEntry` has a
`type` — `V_EMBED` for a standalone `VSET` entry (a single 384-dimensional
embedding), or `V_DOC` for a document created via `VADD` (an
`unordered_map<string, VChunk>` of named chunks, each with its own text and
embedding, keyed by `chunk_name` for O(1) lookup and upsert).

`VSEARCH text [k]` scans only `V_EMBED` entries via `hm_foreach`, ranking by
cosine similarity. `VSEARCH doc_name text [k]` looks up a single `V_DOC`
entry by name and ranks only its chunks — it never scans the full table.
Deleting a document (`VDEL doc_name`) frees the `VEntry` and, via ordinary
C++ destruction, every chunk it owns in one call — no separate cleanup step
is required since chunks are stored by value rather than by pointer.

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
