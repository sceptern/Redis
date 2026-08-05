import redis
import time
import random
import threading

def make_client():
    pool = redis.ConnectionPool(
        host='localhost',
        port=6379,
        socket_connect_timeout=2,
        health_check_interval=0,
        protocol=2,
    )
    return redis.Redis(connection_pool=pool)

def benchmark_zset(r, n):
    print(f"\n{'='*50}")
    print(f"ZSet benchmark with {n} members")
    print(f"{'='*50}")

    zset_key = "bench:zset"

    # ZADD benchmark
    print(f"ZADD {n} members...")
    start = time.time()
    for i in range(n):
        r.zadd(zset_key, {f"member:{i}": random.uniform(0, 1000)})
        if (i + 1) % 10000 == 0:
            elapsed = time.time() - start
            print(f"  {i+1}/{n} inserted, avg: {elapsed/(i+1)*1000:.3f}ms/op")

    total_zadd = time.time() - start
    print(f"\nZADD results:")
    print(f"  Total time:  {total_zadd:.2f}s")
    print(f"  Avg latency: {total_zadd/n*1000:.3f}ms")
    print(f"  Throughput:  {n/total_zadd:.0f} ops/sec")

    # DEL benchmark — deletes the whole zset at once, hits the thread pool
    print(f"\nDEL (entire zset of {n} members)...")
    latencies = []
    for trial in range(5):
        # repopulate
        for i in range(n):
            r.zadd(zset_key, {f"member:{i}": random.uniform(0, 1000)})
        # time the deletion
        start = time.time()
        r.delete(zset_key)
        latencies.append((time.time() - start) * 1000)
        print(f"  trial {trial+1}: {latencies[-1]:.3f}ms")

    latencies.sort()
    print(f"\nDEL results ({n} members):")
    print(f"  Avg latency: {sum(latencies)/len(latencies):.3f}ms")
    print(f"  Min latency: {latencies[0]:.3f}ms")
    print(f"  Max latency: {latencies[-1]:.3f}ms")
    print(f"  p50 latency: {latencies[len(latencies)//2]:.3f}ms")

def concurrent_get(r, results, stop_event):
    while not stop_event.is_set():
        start = time.time()
        r.get("health_check")
        results.append((time.time() - start) * 1000)

def benchmark_del_responsiveness(n):
    print(f"\n{'='*50}")
    print(f"Event loop responsiveness during DEL of {n} members")
    print(f"{'='*50}")

    r = make_client()
    getter = make_client()

    # set up health check key
    r.set("health_check", "ok")

    # populate zset
    print(f"Populating {n} members...")
    for i in range(n):
        r.zadd("bench:zset", {f"member:{i}": random.uniform(0, 1000)})
        if (i + 1) % 10000 == 0:
            print(f"  {i+1}/{n}...")

    # start background GET thread
    get_results = []
    stop = threading.Event()
    t = threading.Thread(target=concurrent_get, args=(getter, get_results, stop))
    t.start()

    time.sleep(0.1)  # let getter warm up
    get_results.clear()  # discard warmup samples

    # fire the DEL
    start = time.time()
    r.delete("bench:zset")
    del_latency = (time.time() - start) * 1000

    time.sleep(0.2)  # collect GET data after DEL completes
    stop.set()
    t.join()

    max_get = max(get_results) if get_results else 0
    avg_get = sum(get_results)/len(get_results) if get_results else 0

    print(f"\nResults:")
    print(f"  DEL latency:          {del_latency:.3f}ms")
    print(f"  GET count during DEL: {len(get_results)}")
    print(f"  GET avg during DEL:   {avg_get:.3f}ms")
    print(f"  GET max during DEL:   {max_get:.3f}ms")

    if max_get < 5:
        print(f"  → event loop stayed responsive (thread pool working)")
    else:
        print(f"  → event loop STALLED for {max_get:.1f}ms during DEL")

    r.delete("health_check")

def main():
    r = make_client()

    # basic zadd/del throughput
    for n in [1000, 10000, 100000, 500000]:
        benchmark_zset(r, n)
        time.sleep(1)

    # event loop responsiveness — the real test of the thread pool
    print(f"\n{'='*50}")
    print("EVENT LOOP RESPONSIVENESS TESTS")
    print(f"{'='*50}")
    for n in [1000, 10000, 100000, 500000]:
        benchmark_del_responsiveness(n)
        time.sleep(1)

if __name__ == "__main__":
    main()
