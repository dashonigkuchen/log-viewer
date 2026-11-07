#pragma once

namespace lv::core
{
class ThreadPool;
}

namespace lv::model
{

// The application-wide thread pool for searches (ARCHITECTURE §3.6: one global pool). Created
// on first use with the default size; destroyed at exit after all documents are gone.
[[nodiscard]] core::ThreadPool& searchThreadPool();

}  // namespace lv::model
