package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"net"
)

type RedisClient struct {
	conn net.Conn
}

func NewRedisClient(host string, port int) (*RedisClient, error) {
	conn, err := net.Dial("tcp", fmt.Sprintf("%s:%d", host, port))
	if err != nil {
		return nil, err
	}
	return &RedisClient{conn: conn}, nil
}

func (r *RedisClient) Close() {
	r.conn.Close()
}

func (r *RedisClient) recvExact(n int) ([]byte, error) {
	buf := make([]byte, n)
	total := 0
	for total < n {
		read, err := r.conn.Read(buf[total:])
		if err != nil {
			return nil, err
		}
		total += read
	}
	return buf, nil
}

func (r *RedisClient) sendRequest(args ...string) (any, error) {
	// build body
	body := make([]byte, 0)
	countBuf := make([]byte, 4)
	binary.LittleEndian.PutUint32(countBuf, uint32(len(args)))
	body = append(body, countBuf...)

	for _, arg := range args {
		encoded := []byte(arg)
		lenBuf := make([]byte, 4)
		binary.LittleEndian.PutUint32(lenBuf, uint32(len(encoded)))
		body = append(body, lenBuf...)
		body = append(body, encoded...)
	}

	// wrap with outer length header
	msg := make([]byte, 0, 4+len(body))
	outerLen := make([]byte, 4)
	binary.LittleEndian.PutUint32(outerLen, uint32(len(body)))
	msg = append(msg, outerLen...)
	msg = append(msg, body...)

	_, err := r.conn.Write(msg)
	if err != nil {
		return nil, err
	}

	return r.readResponse()
}

func (r *RedisClient) readResponse() (any, error) {
	// read outer 4 byte length header
	header, err := r.recvExact(4)
	if err != nil {
		return nil, err
	}
	length := binary.LittleEndian.Uint32(header)

	body, err := r.recvExact(int(length))
	if err != nil {
		return nil, err
	}

	tag := body[0]

	switch tag {
	case 0: // TAG_NIL
		return nil, nil

	case 1: // TAG_ERR
		code := binary.LittleEndian.Uint32(body[1:5])
		msgLen := binary.LittleEndian.Uint32(body[5:9])
		msg := string(body[9 : 9+msgLen])
		return nil, fmt.Errorf("redis error %d: %s", code, msg)

	case 2: // TAG_STR
		strLen := binary.LittleEndian.Uint32(body[1:5])
		return string(body[5 : 5+strLen]), nil

	case 3: // TAG_INT
		val := int64(binary.LittleEndian.Uint64(body[1:9]))
		return val, nil

	case 4: // TAG_DBL
		bits := binary.LittleEndian.Uint64(body[1:9])
		return math.Float64frombits(bits), nil

	case 5: // TAG_ARR
		count := binary.LittleEndian.Uint32(body[1:5])
		results := make([]any, 0, count)
		pos := 5
		for i := 0; i < int(count); i++ {
			itemTag := body[pos]
			pos++
			switch itemTag {
			case 0: // TAG_NIL
				results = append(results, nil)
			case 2: // TAG_STR
				itemLen := int(binary.LittleEndian.Uint32(body[pos : pos+4]))
				pos += 4
				results = append(results, string(body[pos:pos+itemLen]))
				pos += itemLen
			case 3: // TAG_INT
				val := int64(binary.LittleEndian.Uint64(body[pos : pos+8]))
				results = append(results, val)
				pos += 8
			case 4: // TAG_DBL
				bits := binary.LittleEndian.Uint64(body[pos : pos+8])
				results = append(results, math.Float64frombits(bits))
				pos += 8
			}
		}
		return results, nil

	default:
		return nil, fmt.Errorf("unknown tag: %d", tag)
	}
}

func (r *RedisClient) Set(key, value string) error {
	_, err := r.sendRequest("set", key, value)
	return err
}

func (r *RedisClient) Get(key string) (string, error) {
	val, err := r.sendRequest("get", key)
	if err != nil || val == nil {
		return "", err
	}
	return val.(string), nil
}

func (r *RedisClient) Delete(key string) error {
	_, err := r.sendRequest("del", key)
	return err
}

func (r *RedisClient) ZAdd(key string, score float64, name string) error {
	_, err := r.sendRequest("zadd", key, fmt.Sprintf("%f", score), name)
	return err
}

func (r *RedisClient) ZScore(key, name string) (float64, error) {
	val, err := r.sendRequest("zscore", key, name)
	if err != nil || val == nil {
		return 0, err
	}
	return val.(float64), nil
}

func (r *RedisClient) ZRem(key, name string) error {
	_, err := r.sendRequest("zrem", key, name)
	return err
}

func (r *RedisClient) ZQuery(key string, score float64, name string, offset, limit int) ([]any, error) {
	val, err := r.sendRequest("zquery", key, fmt.Sprintf("%f", score), name, fmt.Sprintf("%d", offset), fmt.Sprintf("%d", limit))
	if err != nil || val == nil {
		return nil, err
	}
	return val.([]any), nil
}

func (r *RedisClient) PExpire(key string, ttlMs int) error {
	_, err := r.sendRequest("pexpire", key, fmt.Sprintf("%d", ttlMs))
	return err
}

func (r *RedisClient) PTTL(key string) (int64, error) {
	val, err := r.sendRequest("pttl", key)
	if err != nil || val == nil {
		return 0, err
	}
	return val.(int64), nil
}

func (r *RedisClient) Keys() ([]any, error) {
	val, err := r.sendRequest("keys")
	if err != nil || val == nil {
		return nil, err
	}
	return val.([]any), nil
}
