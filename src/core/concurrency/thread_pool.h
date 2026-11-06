#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace lv::core
{

// Fixed set of worker threads with a FIFO task queue (ARCHITECTURE §3.6). Tasks receive a
// stop token: it is requested when the task's own token (passed to submit) is requested, or
// when the pool shuts down. Cancellation is cooperative; queued tasks whose token is already
// stopped are still run (so they can report completion) but see stop_requested() == true.
//
// Thread-safety: submit() and size() may be called from any thread.
class ThreadPool
{
public:
    using Task = std::function<void(std::stop_token)>;

    // `threads` == 0 picks hardware concurrency - 1 (at least 1).
    explicit ThreadPool(std::size_t threads = 0);
    // Requests stop for all tasks, runs nothing new, and joins the workers. Tasks still queued
    // are run with a stopped token so their completion handlers fire.
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    [[nodiscard]] std::size_t size() const noexcept { return m_workers.size(); }

    // Number of threads `threads == 0` resolves to on this machine.
    [[nodiscard]] static std::size_t defaultThreadCount() noexcept;

    // Queues `task`. `stop` is the caller's token (for example one search); stopping it
    // cancels this task cooperatively.
    void submit(Task task, std::stop_token stop = {});

private:
    struct Entry
    {
        Task task;
        std::stop_token stop;
    };

    void work(const std::stop_token& poolStop);

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Entry> m_queue;
    std::stop_source m_shutdown;
    std::vector<std::jthread> m_workers;
};

}  // namespace lv::core
