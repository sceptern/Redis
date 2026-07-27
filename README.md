# Redis Server

A Redis-compatible key-value store implemented in C++ from scratch, 
featuring a custom binary protocol and RESP compatibility layer.

## Features
- Custom binary protocol on port 1234 for low-overhead clients
- RESP protocol on port 6379 for redis-cli and redis-benchmark compatibility
- String and sorted set (ZSet) data types via AVL tree + hashtable
- Key expiry via TTL (PEXPIRE/PTTL) using a min-heap
- Idle connection timeouts via doubly linked list
- Thread pool for async deletion of large data structures
- Non-blocking I/O event loop using poll()

## Commands
| Command | Description |
|---------|-------------|
| GET key | Get string value |
| SET key value | Set string value |
| DEL key | Delete key |
| KEYS | List all keys |
| PEXPIRE key ms | Set TTL in milliseconds |
| PTTL key | Get remaining TTL |
| ZADD zset score name | Add to sorted set |
| ZREM zset name | Remove from sorted set |
| ZSCORE zset name | Get score |
| ZQUERY zset score name offset limit | Range query |

## Performance
Benchmarked on WSL2 against Redis 7.0.15 (persistence disabled, 
1M requests, 1024 byte payload, 50 concurrent clients):

| Metric | This Server | Redis 7.0 |
|--------|-------------|-----------|
| SET throughput | 65,737 req/s | 59,580 req/s |
| GET throughput | 75,018 req/s | 65,763 req/s |
| p50 latency | 0.343ms | 0.399ms |
| p99 latency | 0.775ms | 0.883ms |

Note: throughput advantage reflects Redis's additional per-request 
overhead (keyspace notifications, ACLs, stats tracking) rather than 
a fundamental architectural difference.

## Build
\```bash
g++ -O2 -march=native -o server server.cpp hashtable.cpp avl.cpp \
    zset.cpp heap.cpp thread_pool.cpp -lm
\```

## Usage
\```bash
# start server (binary on 1234, RESP on 6379)
./server

# connect with redis-cli
redis-cli -p 6379 SET foo bar

# connect with binary client
./client SET foo bar

# benchmark
redis-benchmark -p 6379 -t set,get -n 100000
\```

## Architecture
- Event loop: single-threaded poll() with non-blocking I/O
- Hash table: open addressing with incremental resizing
- Sorted sets: AVL tree + hashtable for O(log n) operations
- TTL: min-heap ordered by expiry time
- Connection timeouts: doubly linked list ordered by last active time
- Large deletions: offloaded to thread pool to avoid blocking event loop
