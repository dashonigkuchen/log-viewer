#include "core/concurrency/thread_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <latch>
#include <mutex>
#include <set>
#include <thread>

using lv::core::ThreadPool;
using namespace std::chrono_literals;

TEST_CASE("thread pool runs all tasks", "[concurrency][pool]")
{
    std::atomic<int> sum{0};
    std::set<std::thread::id> threads;
    std::mutex mutex;
    {
        ThreadPool pool(3);
        CHECK(pool.size() == 3);
        std::latch done(100);
        for (int i = 1; i <= 100; ++i) {
            pool.submit([&, i](const std::stop_token&) {
                sum += i;
                {
                    const std::scoped_lock lock(mutex);
                    threads.insert(std::this_thread::get_id());
                }
                std::this_thread::sleep_for(100us);
                done.count_down();
            });
        }
        done.wait();
    }
    CHECK(sum == 5050);
    CHECK(threads.size() <= 3);
    CHECK_FALSE(threads.contains(std::this_thread::get_id()));
}

TEST_CASE("thread pool default size", "[concurrency][pool]")
{
    const ThreadPool pool;
    CHECK(pool.size() == ThreadPool::defaultThreadCount());
    CHECK(pool.size() >= 1);
}

TEST_CASE("stop token cancels running and queued tasks", "[concurrency][pool]")
{
    ThreadPool pool(1);
    // NOLINTNEXTLINE(misc-const-correctness): request_stop() is non-const in MSVC's STL
    std::stop_source search;
    std::latch running(1);
    std::atomic<bool> sawStopWhileRunning{false};
    std::atomic<bool> queuedSawStop{false};
    std::latch finished(2);
    pool.submit(
        [&](const std::stop_token& stop) {
            running.count_down();
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(1ms);
            }
            sawStopWhileRunning = true;
            finished.count_down();
        },
        search.get_token());
    pool.submit(
        [&](const std::stop_token& stop) {
            queuedSawStop = stop.stop_requested();
            finished.count_down();
        },
        search.get_token());
    running.wait();
    search.request_stop();
    finished.wait();
    CHECK(sawStopWhileRunning);
    CHECK(queuedSawStop);
}

TEST_CASE("other tasks are not affected by a stopped search", "[concurrency][pool]")
{
    ThreadPool pool(2);
    // NOLINTNEXTLINE(misc-const-correctness): request_stop() is non-const in MSVC's STL
    std::stop_source a;
    // NOLINTNEXTLINE(misc-const-correctness): request_stop() is non-const in MSVC's STL
    std::stop_source b;
    a.request_stop();
    std::atomic<bool> aStopped{false};
    std::atomic<bool> bStopped{true};
    std::latch done(2);
    pool.submit(
        [&](const std::stop_token& stop) {
            aStopped = stop.stop_requested();
            done.count_down();
        },
        a.get_token());
    pool.submit(
        [&](const std::stop_token& stop) {
            bStopped = stop.stop_requested();
            done.count_down();
        },
        b.get_token());
    done.wait();
    CHECK(aStopped);
    CHECK_FALSE(bStopped);
}

TEST_CASE("destruction stops long tasks and drains the queue", "[concurrency][pool]")
{
    std::atomic<int> ran{0};
    std::atomic<int> stoppedSeen{0};
    const auto start = std::chrono::steady_clock::now();
    {
        ThreadPool pool(2);
        for (int i = 0; i < 20; ++i) {
            pool.submit([&](const std::stop_token& stop) {
                ++ran;
                // Would run for 10 s without cancellation.
                for (int k = 0; k < 10000 && !stop.stop_requested(); ++k) {
                    std::this_thread::sleep_for(1ms);
                }
                if (stop.stop_requested()) {
                    ++stoppedSeen;
                }
            });
        }
        std::this_thread::sleep_for(20ms);
    }
    CHECK(ran == 20);  // queued tasks still ran (with a stopped token)
    CHECK(stoppedSeen == 20);
    CHECK(std::chrono::steady_clock::now() - start < 5s);
}

TEST_CASE("more tasks than threads, tasks submitting tasks", "[concurrency][pool]")
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    std::latch done(1000);
    for (int i = 0; i < 500; ++i) {
        pool.submit([&](const std::stop_token&) {
            ++count;
            done.count_down();
            pool.submit([&](const std::stop_token&) {
                ++count;
                done.count_down();
            });
        });
    }
    done.wait();
    CHECK(count == 1000);
}
