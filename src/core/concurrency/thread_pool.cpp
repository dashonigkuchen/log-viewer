#include "core/concurrency/thread_pool.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace lv::core
{

namespace
{

// A stop source that is requested when either input token is.
class CombinedStop
{
public:
    CombinedStop(const std::stop_token& a, const std::stop_token& b)
        : m_onA(a, Request{&m_source})
        , m_onB(b, Request{&m_source})
    {}

    [[nodiscard]] std::stop_token token() const noexcept { return m_source.get_token(); }

private:
    struct Request
    {
        std::stop_source* source;

        void operator()() const noexcept { source->request_stop(); }
    };

    std::stop_source m_source;
    std::stop_callback<Request> m_onA;
    std::stop_callback<Request> m_onB;
};

}  // namespace

std::size_t ThreadPool::defaultThreadCount() noexcept
{
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware > 1 ? hardware - 1 : 1;
}

ThreadPool::ThreadPool(std::size_t threads)
{
    const std::size_t count = threads == 0 ? defaultThreadCount() : threads;
    m_workers.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        m_workers.emplace_back([this, stop = m_shutdown.get_token()] { work(stop); });
    }
}

ThreadPool::~ThreadPool()
{
    m_shutdown.request_stop();  // wakes waiting workers (see work())
    m_workers.clear();          // joins; workers drain the queue first
}

void ThreadPool::submit(Task task, std::stop_token stop)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_queue.push_back({std::move(task), std::move(stop)});
    }
    m_wake.notify_one();
}

void ThreadPool::work(const std::stop_token& poolStop)
{
    while (true) {
        std::optional<Entry> entry;
        {
            std::unique_lock lock(m_mutex);
            // The stop_token overload also wakes up when stop is requested, without the lost
            // wakeup a separate notify_all() after request_stop() could cause.
            m_wake.wait(lock, poolStop, [&] { return !m_queue.empty(); });
            if (m_queue.empty()) {
                return;  // shutdown and nothing left
            }
            entry.emplace(std::move(m_queue.front()));
            m_queue.pop_front();
        }
        const CombinedStop stop(entry->stop, poolStop);
        entry->task(stop.token());
    }
}

}  // namespace lv::core
