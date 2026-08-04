import redis
import time
import random

def make_client():
    pool = redis.ConnectionPool(
        host='localhost',
        port=6379,
        socket_connect_timeout=2,
        health_check_interval=0,
        protocol=2,
    )
    return redis.Redis(connection_pool=pool)

def random_text():
    words = ["dog", "cat", "puppy", "kitten", "stock", "market", "weather",
             "sunny", "rain", "happy", "sad", "fast", "slow", "big", "small",
             "red", "blue", "green", "river", "mountain", "ocean", "forest"]
    return " ".join(random.choices(words, k=random.randint(4, 10)))

def benchmark(r, n):
    print(f"\n{'='*50}")
    print(f"Benchmarking with {n} vectors")
    print(f"{'='*50}")

    texts = [random_text() for _ in range(n)]

    # insertion benchmark
    print(f"Inserting {n} vectors...")
    start = time.time()
    for i, text in enumerate(texts):
        r.execute_command("VSET", text)
        if (i + 1) % 100 == 0:
            elapsed = time.time() - start
            print(f"  {i+1}/{n} inserted, avg latency: {elapsed/(i+1)*1000:.2f}ms/insert")

    total_insert = time.time() - start
    print(f"\nInsertion results:")
    print(f"  Total time:     {total_insert:.2f}s")
    print(f"  Avg per insert: {total_insert/n*1000:.2f}ms")

    # search benchmark
    queries = [random_text() for _ in range(20)]
    print(f"\nSearching (top-5), 20 queries...")
    latencies = []
    for q in queries:
        start = time.time()
        r.execute_command("VSEARCH", q, 5)
        latencies.append((time.time() - start) * 1000)

    latencies.sort()
    print(f"  Avg latency: {sum(latencies)/len(latencies):.2f}ms")
    print(f"  p50 latency: {latencies[len(latencies)//2]:.2f}ms")
    print(f"  p95 latency: {latencies[int(len(latencies)*0.95)]:.2f}ms")
    print(f"  Min latency: {latencies[0]:.2f}ms")
    print(f"  Max latency: {latencies[-1]:.2f}ms")

    # cleanup
    print(f"\nCleaning up...")
    for text in texts:
        r.execute_command("VDEL", text)

def main():
    r = make_client()
    try:
        r.execute_command("PING")
    except Exception as e:
        print(f"Could not connect to server: {e}")
        return

    for n in [100, 1000, 10000]:
        benchmark(r, n)
        time.sleep(1)

if __name__ == "__main__":
    main()