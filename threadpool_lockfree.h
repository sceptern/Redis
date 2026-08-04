#pragma once

#include <stddef.h>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <semaphore.h>
#include <sched.h>

struct Work {
    void (*f)(void *) = nullptr;
    void *arg = nullptr;
};

struct Cell {
    std::atomic<size_t> sequence{0};
    Work data;
};

struct ThreadPool {
    std::unique_ptr<Cell[]> buf;

    size_t capacity = 0;
    size_t mask = 0;

    size_t enqueue_pos = 0;
    std::atomic<size_t> dequeue_pos{0};
    std::vector<std::thread> threads;

    sem_t items;
    std::atomic<bool> stop{false};
};

inline void thread_pool_init(
    ThreadPool *tp,
    size_t num_threads,
    size_t capacity_pow2 = 4096);

inline void thread_pool_queue(
    ThreadPool *tp,
    void (*f)(void *),
    void *arg);

inline void thread_pool_destroy(ThreadPool *tp);