import socket
import struct

class RedisClient:
    def __init__(self, host, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))

    def _recv_exact(self, n):
        data = bytearray()
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise Exception("connection closed")
            data.extend(chunk)
        return bytes(data)
    
    def _send_request(self, *args):
        body = bytearray()
        body.extend(struct.pack('<I', len(args)))

        # write each argument
        for arg in args:
            encoded = arg.encode()
            body.extend(struct.pack('<I', len(encoded)))
            body.extend(encoded)
            
        # wrap the outer transport layer
        msg = bytearray()
        msg.extend(struct.pack('<I', len(body)))
        msg.extend(body)

        self.sock.sendall(bytes(msg))
        return self._read_response()
        
    def _read_response(self):
        # read outer 4 byte length header
        header = self._recv_exact(4)
        length = struct.unpack('<I', header)[0]
    
        # read body
        body = self._recv_exact(length)
    
        # parse tag (first byte)
        tag = body[0]
    
        if tag == 0:    # TAG_NIL
            return None
    
        elif tag == 1:  # TAG_ERR
            code = struct.unpack('<I', body[1:5])[0]
            msg_len = struct.unpack('<I', body[5:9])[0]
            msg = body[9:9 + msg_len].decode()
            raise Exception(f"Redis error {code}: {msg}")
    
        elif tag == 2:  # TAG_STR
            str_len = struct.unpack('<I', body[1:5])[0]
            return body[5:5 + str_len].decode()
    
        elif tag == 3:  # TAG_INT
            return struct.unpack('<q', body[1:9])[0]
    
        elif tag == 4:  # TAG_DBL
            return struct.unpack('<d', body[1:9])[0]
    
        elif tag == 5:  # TAG_ARR
            count = struct.unpack('<I', body[1:5])[0]
            # array items are packed sequentially in body
            results = []
            pos = 5  # start after tag + count
            for _ in range(count):
                item_tag = body[pos]
                pos += 1
                if item_tag == 2:   # TAG_STR
                    item_len = struct.unpack('<I', body[pos:pos+4])[0]
                    pos += 4
                    results.append(body[pos:pos+item_len].decode())
                    pos += item_len
                elif item_tag == 4: # TAG_DBL
                    results.append(struct.unpack('<d', body[pos:pos+8])[0])
                    pos += 8
                elif item_tag == 3: # TAG_INT
                    results.append(struct.unpack('<q', body[pos:pos+8])[0])
                    pos += 8
                elif item_tag == 0: # TAG_NIL
                    results.append(None)
            return results
    
        else:
            raise Exception(f"unknown tag: {tag}")
    def set(self, key, value):
        return self._send_request('set', key, value)
    def get(self, key):
        return self._send_request('get', key)
    def delete(self, key):
        return self._send_request('del', key)

    def zadd(self, key, score, name):
        return self._send_request('zadd', key, str(score), name)

    def zscore(self, key, name):
        return self._send_request('zscore', key, name)

    def zrem(self, key, name):
        return self._send_request('zrem', key, name)

    def zquery(self, key, score, name, offset, limit):
        return self._send_request('zquery', key, str(score), name, str(offset), str(limit))

    def pexpire(self, key, ttl_ms):
        return self._send_request('pexpire', key, str(ttl_ms))

    def pttl(self, key):
        return self._send_request('pttl', key)

    def keys(self):
        return self._send_request('keys')
    def close(self):
        self.sock.close()

if __name__ == '__main__':
    client = RedisClient('localhost', 1234)
    # basic set/get
    print("=== SET/GET ===")
    client.set('foo', 'bar')
    print(client.get('foo'))           # bar
    client.set('hello', 'world')
    print(client.get('hello'))         # world
    print(client.get('nonexistent'))   # None

    # delete
    print("\n=== DEL ===")
    client.set('todelete', 'value')
    print(client.delete('todelete'))   # 1
    print(client.get('todelete'))      # None
    print(client.delete('todelete'))   # 0 (already gone)

    # keys
    print("\n=== KEYS ===")
    print(client.keys())               # ['foo', 'hello']

    # ttl
    print("\n=== TTL ===")
    client.set('expiring', 'value')
    print(client.pttl('expiring'))     # -1 (no ttl)
    client.pexpire('expiring', 5000)   # expire in 5 seconds
    print(client.pttl('expiring'))     # ~5000
    print(client.pttl('nonexistent'))  # -2 (key doesn't exist)

    # sorted set
    print("\n=== ZSET ===")
    client.zadd('leaderboard', 100, 'alice')
    client.zadd('leaderboard', 200, 'bob')
    client.zadd('leaderboard', 150, 'carol')
    print(client.zscore('leaderboard', 'alice'))   # 100.0
    print(client.zscore('leaderboard', 'bob'))     # 200.0
    print(client.zscore('leaderboard', 'nobody'))  # None

    # zquery - get all pairs sorted by score
    print("\n=== ZQUERY ===")
    print(client.zquery('leaderboard', 0, '', 0, 10))
    # ['alice', 100.0, 'carol', 150.0, 'bob', 200.0]

    # zquery with offset
    print(client.zquery('leaderboard', 0, '', 2, 4))
    # ['carol', 150.0, 'bob', 200.0] (skip first 2 values)

    # zrem
    print("\n=== ZREM ===")
    print(client.zrem('leaderboard', 'alice'))     # 1
    print(client.zscore('leaderboard', 'alice'))   # None
    print(client.zquery('leaderboard', 0, '', 0, 10))
    # ['carol', 150.0, 'bob', 200.0]

    # wrong type errors
    print("\n=== TYPE ERRORS ===")
    try:
        client.get('leaderboard')  # leaderboard is a zset not string
    except Exception as e:
        print(e)  # Redis error 3: not a string value

    
    print("\n=== KEYS ===")
    print(client.keys()) 

    client.close()
