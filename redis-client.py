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
            sg_len = struct.unpack('<I', body[5:9])[0]
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

if __name__ == '__main__':
    client = RedisClient('localhost', 1234)
    
    client.set('foo', 'bar')
    print(client.get('foo'))   # should print 'bar'
    
    client.set('hello', 'world')
    print(client.get('hello')) # should print 'world'
    
    print(client.get('nonexistent'))  # should print None
