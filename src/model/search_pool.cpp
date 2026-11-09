#include "model/search_pool.h"

#include "core/concurrency/thread_pool.h"

namespace lv::model
{

namespace
{

std::size_t& requestedThreads()
{
    static std::size_t threads = 0;
    return threads;
}

}  // namespace

void setSearchThreadCount(int threads)
{
    requestedThreads() = threads > 0 ? static_cast<std::size_t>(threads) : 0;
}

core::ThreadPool& searchThreadPool()
{
    static core::ThreadPool pool(requestedThreads());
    return pool;
}

}  // namespace lv::model
