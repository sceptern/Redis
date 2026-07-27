# Redis Server

A Redis-compatible key-value store implemented in C++ from scratch,
featuring a custom binary protocol, RESP compatibility layer, and both
poll() and epoll() event loop implementations.

## Features
- Custom binary protocol on port 1234 for low-overhead clients
- RESP protocol on port 6379 for redis-cli and redis-benchmark compatibility
- String and sorted set (ZSet) data types via AVL tree + hashtable
- Key expiry via TTL (PEXPIRE/PTTL) using a min-heap
- Idle connection timeouts via doubly linked list
- Thread pool for async deletion of large data structures
- Two event loop implementations: poll() and epoll()
- Configurable max-events and idle-timeout for high connection count scenarios

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

### Throughput vs Redis 7.0
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
a fundamental architectural difference. Bare-metal Redis typically
achieves 100k+ ops/sec; WSL2 networking overhead affects both servers equally.

### epoll vs poll at high connection counts
100k requests, 1024 byte payload:

| Clients | epoll ops/sec | poll ops/sec | epoll max latency | poll max latency |
|---------|--------------|--------------|-------------------|------------------|
| 50 | 67k | 75k | 1.5ms | 1.9ms |
| 200 | 60k | 70k | 4.4ms | 4.4ms |
| 1000 | 53k | 63k | 16ms | 14ms |
| 5000 | 52k | 52k | 76ms | 549ms |
| 10000 | 59k | 13k | 141ms | 3000ms (timeouts) |

poll() collapses at 10k connections due to O(n) fd scanning. epoll's
O(1) event notification maintains throughput and keeps max latency
under 141ms even at 10k concurrent connections.

## Build
```bash
# poll-based (simple, good for low connection counts)
g++ -O2 -march=native -o server server.cpp hashtable.cpp avl.cpp \
    zset.cpp heap.cpp thread_pool.cpp -lm

# epoll-based (high connection count, configurable)
g++ -O2 -march=native -o server_epoll server_epoll.cpp hashtable.cpp avl.cpp \
    zset.cpp heap.cpp thread_pool.cpp -lm
```

## Usage
```bash
# start poll server (binary on 1234, RESP on 6379)
./server

# start epoll server with defaults
./server_epoll

# start epoll server for high connection count
./server_epoll --max-events 10000 --idle-timeout 60000

# connect with redis-cli
redis-cli -p 6379 SET foo bar

# connect with binary client
./client SET foo bar

# benchmark
redis-benchmark -p 6379 -t set,get -n 100000
redis-benchmark -p 6379 -t set,get -n 100000 -c 10000
```

## Architecture
- Event loop: single-threaded poll() or epoll() with non-blocking I/O
- Hash table: open addressing with incremental resizing
- Sorted sets: AVL tree + hashtable for O(log n) operations
- TTL: min-heap ordered by expiry time
- Connection timeouts: doubly linked list ordered by last active time
- Large deletions: offloaded to thread pool to avoid blocking event loop
- Dual protocol: binary on port 1234, RESP on port 6379, detected at accept() time
