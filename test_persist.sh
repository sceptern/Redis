#!/bin/bash

PASS=0
FAIL=0

check() {
    local desc=$1
    local expected=$2
    local actual=$3

    if [ "$actual" = "$expected" ]; then
        echo "  PASS: $desc"
        ((PASS++))
    else
        echo "  FAIL: $desc (expected '$expected', got '$actual')"
        ((FAIL++))
    fi
}

wait_for_server() {
    echo "waiting for server..."
    for i in $(seq 1 20); do
        if redis-cli -p 6379 PING >/dev/null 2>&1; then
            echo "server ready"
            return 0
        fi
        sleep 1
    done

    echo "server never became ready"
    return 1
}

start_server() {
    ./server_epoll >/dev/null 2>&1 &
    SERVER_PID=$!

    echo "Started server PID: $SERVER_PID"

    wait_for_server

    echo "Listening process:"
    lsof -i :6379
}

stop_server() {
    echo
    echo "Stopping server PID: $SERVER_PID"

    ps -fp "$SERVER_PID"

    kill -TERM "$SERVER_PID"

    echo "kill exit code: $?"

    wait "$SERVER_PID" 2>/dev/null

    sleep 1

    echo
    echo "Process after kill:"
    ps -fp "$SERVER_PID"

    echo
    echo "Port 6379:"
    lsof -i :6379

    echo
}

echo "=== Persistence Test ==="

fuser -k 6379/tcp >/dev/null 2>&1 || true
fuser -k 1234/tcp >/dev/null 2>&1 || true
sleep 2

# First start
start_server

redis-cli -p 6379 SET foo bar >/dev/null
redis-cli -p 6379 SET hello world >/dev/null
redis-cli -p 6379 SET num 42 >/dev/null

echo "waiting for autosave..."
sleep 4

# First stop
stop_server

sleep 2

# Restart
start_server

check "foo persisted"   "bar"   "$(redis-cli -p 6379 GET foo)"
check "hello persisted" "world" "$(redis-cli -p 6379 GET hello)"
check "num persisted"   "42"    "$(redis-cli -p 6379 GET num)"
check "missing key"     ""      "$(redis-cli -p 6379 GET doesnotexist)"

# Final stop
stop_server

echo
echo "Results: $PASS passed, $FAIL failed"

if [ "$FAIL" -eq 0 ]; then
    exit 0
else
    exit 1
fi