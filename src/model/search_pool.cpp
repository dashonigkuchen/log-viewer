#include "model/search_pool.h"

#include "core/concurrency/thread_pool.h"

namespace lv::model
{

core::ThreadPool& searchThreadPool()
{
    static core::ThreadPool pool;
    return pool;
}

}  // namespace lv::model
