#include "threadpool_lockfree.h"

inline void thread_pool_init(
    ThreadPool* tp,
    size_t num_threads,
    size_t capacity_pow2)
{
    // Allocate the ring buffer.
    tp->buf = std::make_unique<Cell[]>(capacity_pow2);

    tp->capacity = capacity_pow2;
    tp->mask = capacity_pow2 - 1;

    // Initially every slot is empty.
    for (size_t i = 0; i < capacity_pow2; i++) {
        tp->buf[i].sequence.store(i, std::memory_order_relaxed);
    }

    tp->enqueue_pos = 0;
    tp->dequeue_pos.store(0, std::memory_order_relaxed);

    tp->stop.store(false, std::memory_order_relaxed);

    sem_init(&tp->items, 0, 0);

    auto worker_func = [tp]() {

        while (true) {

            // Sleep until at least one job exists.
            sem_wait(&tp->items);

            // Pool shutting down?
            if (tp->stop.load(std::memory_order_acquire))
                return;

            Work work;

            //
            // Try to claim exactly one job.
            //
            size_t dequeue_ticket =
                tp->dequeue_pos.load(std::memory_order_relaxed);

            while (true) {

                Cell& cell =
                    tp->buf[dequeue_ticket & tp->mask];

                size_t slot_sequence =
                    cell.sequence.load(std::memory_order_acquire);

                intptr_t difference =
                    (intptr_t)slot_sequence -
                    (intptr_t)(dequeue_ticket + 1);

                //
                // Slot contains exactly the job we want.
                //
                if (difference == 0) {

                    bool claimed =
                        tp->dequeue_pos.compare_exchange_weak(
                            dequeue_ticket,
                            dequeue_ticket + 1,
                            std::memory_order_relaxed
                        );

                    if (claimed) {

                        work = cell.data;

                        // Mark this slot empty again.
                        cell.sequence.store(
                            dequeue_ticket + tp->mask + 1,
                            std::memory_order_release
                        );

                        break;
                    }
                }

                //
                // Producer hasn't finished writing yet.
                //
                else if (difference < 0) {

                    sched_yield();

                    dequeue_ticket =
                        tp->dequeue_pos.load(
                            std::memory_order_relaxed
                        );
                }

                //
                // Somebody else already consumed this slot.
                //
                else {

                    dequeue_ticket =
                        tp->dequeue_pos.load(
                            std::memory_order_relaxed
                        );
                }
            }

            work.f(work.arg);
        }
    };

    for (size_t i = 0; i < num_threads; i++) {

        std::thread worker(worker_func);

        tp->threads.push_back(std::move(worker));
    }
}

inline void thread_pool_queue(
    ThreadPool* tp,
    void (*f)(void*),
    void* arg)
{
    //
    // Only one producer exists,
    // so enqueue_pos doesn't need to be atomic.
    //
    size_t enqueue_ticket = tp->enqueue_pos;

    Cell& cell =
        tp->buf[enqueue_ticket & tp->mask];

    size_t slot_sequence =
        cell.sequence.load(std::memory_order_acquire);

    //
    // If sequence != enqueue_ticket,
    // the queue is currently full.
    //
    if (slot_sequence != enqueue_ticket) {

        //
        // Execute immediately instead of blocking.
        //
        f(arg);
        return;
    }

    //
    // Write the job.
    //
    cell.data = {f, arg};

    //
    // Publish it.
    //
    cell.sequence.store(
        enqueue_ticket + 1,
        std::memory_order_release);

    tp->enqueue_pos++;

    sem_post(&tp->items);
}

inline void thread_pool_destroy(ThreadPool* tp)
{
    tp->stop.store(
        true,
        std::memory_order_release);

    //
    // Wake every sleeping worker.
    //
    for (size_t i = 0; i < tp->threads.size(); i++) {
        sem_post(&tp->items);
    }

    //
    // Wait for them to finish.
    //
    for (auto& worker : tp->threads) {
        worker.join();
    }

    sem_destroy(&tp->items);
}