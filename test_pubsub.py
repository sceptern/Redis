import redis
import threading
import time

def make_client():
    pool = redis.ConnectionPool(
        host='localhost',
        port=6379,
        socket_connect_timeout=2,
        health_check_interval=0,
        protocol=2,
    )
    return redis.Redis(connection_pool=pool)


# TEST 1: basic subscribe + publish
print("TEST 1: basic pub/sub...")
received = []

def subscriber():
    sub = make_client()
    p = sub.pubsub()
    p.subscribe('news')
    msg = p.get_message(timeout=1)
    assert msg is not None, "No subscribe confirmation received"
    assert msg['type'] == 'subscribe', f"Expected subscribe confirmation, got {msg}"
    assert msg['channel'] == b'news'
    assert msg['data'] == 1

    msg = p.get_message(timeout=2)
    assert msg is not None, "No message received"
    assert msg['type'] == 'message'
    assert msg['channel'] == b'news'
    assert msg['data'] == b'hello'
    received.append(msg)
    p.close()

t = threading.Thread(target=subscriber)
t.start()
time.sleep(0.2)
r = make_client()
r.publish('news', 'hello')
t.join(timeout=3)
assert len(received) == 1, "Message was not received by subscriber"
print("  PASS")


# TEST 2: publish to channel with no subscribers returns 0
print("TEST 2: publish to empty channel...")
r = make_client()
count = r.publish('nobody', 'hello')
assert count == 0, f"Expected 0, got {count}"
print("  PASS")


# TEST 3: multiple subscribers all receive the same message
print("TEST 3: multiple subscribers...")
results = []
lock = threading.Lock()

def make_sub(name):
    def run():
        sub = make_client()
        p = sub.pubsub()
        p.subscribe('broadcast')
        p.get_message(timeout=1)  # consume subscribe confirmation
        msg = p.get_message(timeout=3)
        assert msg is not None, f"{name} got no message"
        assert msg['data'] == b'ping', f"{name} got wrong message: {msg['data']}"
        with lock:
            results.append(name)
        p.close()
    return run

threads = [threading.Thread(target=make_sub(f"sub{i}")) for i in range(3)]
for t in threads:
    t.start()
time.sleep(0.3)
r = make_client()
count = r.publish('broadcast', 'ping')
assert count == 3, f"Expected 3 receivers, got {count}"
for t in threads:
    t.join(timeout=4)
assert len(results) == 3, f"Only {len(results)}/3 subscribers got the message"
print("  PASS")


# TEST 4: subscriber disconnects cleanly, publish afterwards returns 0
print("TEST 4: subscriber disconnect cleanup...")
sub = make_client()
p = sub.pubsub()
p.subscribe('temp')
p.get_message(timeout=1)
p.close()
sub.close()
time.sleep(0.3)  # give server time to process disconnect
r = make_client()
count = r.publish('temp', 'ghost')
assert count == 0, f"Expected 0 after disconnect, got {count}"
print("  PASS")


# TEST 5: multiple messages to same subscriber arrive in order
print("TEST 5: message ordering...")
order = []

def ordered_sub():
    sub = make_client()
    p = sub.pubsub()
    p.subscribe('ordered')
    p.get_message(timeout=1)
    for _ in range(5):
        msg = p.get_message(timeout=2)
        assert msg is not None, "Missing message"
        order.append(msg['data'])
    p.close()

t = threading.Thread(target=ordered_sub)
t.start()
time.sleep(0.2)
r = make_client()
for i in range(5):
    r.publish('ordered', str(i))
t.join(timeout=5)
assert order == [b'0', b'1', b'2', b'3', b'4'], f"Wrong order: {order}"
print("  PASS")


# TEST 6: subscribe to multiple channels at once
print("TEST 6: subscribe to multiple channels...")
multi_received = []

def multi_sub():
    sub = make_client()
    p = sub.pubsub()
    p.subscribe('ch1', 'ch2', 'ch3')
    # consume 3 subscribe confirmations
    for _ in range(3):
        msg = p.get_message(timeout=1)
        assert msg is not None and msg['type'] == 'subscribe'
    # receive one message on each channel
    for _ in range(3):
        msg = p.get_message(timeout=2)
        assert msg is not None and msg['type'] == 'message'
        multi_received.append(msg['channel'])
    p.close()

t = threading.Thread(target=multi_sub)
t.start()
time.sleep(0.2)
r = make_client()
r.publish('ch1', 'a')
r.publish('ch2', 'b')
r.publish('ch3', 'c')
t.join(timeout=5)
assert len(multi_received) == 3, f"Expected 3 messages, got {len(multi_received)}"
assert set(multi_received) == {b'ch1', b'ch2', b'ch3'}, f"Wrong channels: {multi_received}"
print("  PASS")


# TEST 7: rapid connect/disconnect stress test (hits conn_destroy cleanup path)
print("TEST 7: rapid subscriber disconnect stress...")
r = make_client()
for i in range(50):
    sub = make_client()
    p = sub.pubsub()
    p.subscribe('stress')
    p.get_message(timeout=1)
    p.close()
    sub.close()
time.sleep(0.3)
count = r.publish('stress', 'after')
assert count == 0, f"Expected 0 after all disconnects, got {count}"
print("  PASS")


# TEST 8: unsubscribe from a specific channel
print("TEST 8: unsubscribe from specific channel...")
sub = make_client()
p = sub.pubsub()
p.subscribe('unsub_test')
p.get_message(timeout=1)  # consume subscribe confirmation

p.unsubscribe('unsub_test')
msg = p.get_message(timeout=1)
assert msg is not None, "No unsubscribe confirmation received"
assert msg['type'] == 'unsubscribe'
assert msg['channel'] == b'unsub_test'
assert msg['data'] == 0  # 0 subscriptions remaining

# publish should now reach nobody
r = make_client()
count = r.publish('unsub_test', 'should_not_arrive')
assert count == 0, f"Expected 0 after unsubscribe, got {count}"
p.close()
sub.close()
print("  PASS")


# TEST 9: unsubscribe from one channel, still receive on another
print("TEST 9: partial unsubscribe...")
still_received = []

def partial_unsub():
    sub = make_client()
    p = sub.pubsub()
    p.subscribe('keep', 'drop')
    p.get_message(timeout=1)  # confirmation for 'keep'
    p.get_message(timeout=1)  # confirmation for 'drop'

    p.unsubscribe('drop')
    msg = p.get_message(timeout=1)
    assert msg is not None and msg['type'] == 'unsubscribe'
    assert msg['channel'] == b'drop'
    assert msg['data'] == 1  # still subscribed to 'keep'

    # should still receive on 'keep'
    msg = p.get_message(timeout=2)
    assert msg is not None, "Should still receive on 'keep'"
    assert msg['type'] == 'message'
    assert msg['channel'] == b'keep'
    assert msg['data'] == b'hello'
    still_received.append(msg)
    p.close()

t = threading.Thread(target=partial_unsub)
t.start()
time.sleep(0.3)
r = make_client()
r.publish('drop', 'should_not_arrive')
r.publish('keep', 'hello')
t.join(timeout=4)
assert len(still_received) == 1, "Should have received exactly 1 message on 'keep'"
time.sleep(0.1)
count = r.publish('drop', 'again')
assert count == 0, f"'drop' should have no subscribers, got {count}"
print("  PASS")


# TEST 10: unsubscribe from all channels at once (no args)
print("TEST 10: unsubscribe from all channels...")
sub = make_client()
p = sub.pubsub()
p.subscribe('all1', 'all2', 'all3')
for _ in range(3):
    p.get_message(timeout=1)  # consume subscribe confirmations

p.unsubscribe()  # no args = unsubscribe all
confirmations = []
for _ in range(3):
    msg = p.get_message(timeout=1)
    assert msg is not None, "Missing unsubscribe confirmation"
    assert msg['type'] == 'unsubscribe'
    confirmations.append(msg['channel'])

assert set(confirmations) == {b'all1', b'all2', b'all3'}, \
    f"Wrong channels in confirmations: {confirmations}"

r = make_client()
for ch in ['all1', 'all2', 'all3']:
    count = r.publish(ch, 'test')
    assert count == 0, f"Expected 0 subscribers on {ch}, got {count}"

p.close()
sub.close()
print("  PASS")


# TEST 11: normal commands work after full unsubscribe
print("TEST 11: normal commands work after full unsubscribe...")
sub = make_client()
p = sub.pubsub()
p.subscribe('restore_test')
p.get_message(timeout=1)
p.unsubscribe('restore_test')
p.get_message(timeout=1)  # consume unsubscribe confirmation
p.close()

sub.set('foo', 'bar')
val = sub.get('foo')
assert val == b'bar', f"Expected b'bar', got {val}"
sub.delete('foo')
sub.close()
print("  PASS")


print("\nAll tests passed.")