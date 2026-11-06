// Minimal dependency-free test harness. Swap for GoogleTest later if you like.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "threadpool/thread_pool.hpp"

namespace {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            throw Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                          "  CHECK failed: " #cond);                               \
        }                                                                          \
    } while (0)

// Fail a hung test instead of blocking forever.
template <class F>
void with_timeout(std::chrono::seconds limit, F&& body) {
    std::promise<void> done;
    auto fut = done.get_future();
    std::thread t([&] {
        try {
            body();
            done.set_value();
        } catch (...) {
            done.set_exception(std::current_exception());
        }
    });
    if (fut.wait_for(limit) == std::future_status::timeout) {
        std::fprintf(stderr, "    TIMEOUT (likely deadlock or missed notify)\n");
        std::fflush(stderr);
        std::_Exit(2);  // can't safely join a hung thread
    }
    t.join();
    fut.get();  // rethrow
}

using namespace tp;
using namespace std::chrono_literals;

void runs_all_tasks() {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i) pool.submit([&] { counter.fetch_add(1); });
        pool.wait_idle();
        CHECK(counter.load() == 1000);
    }
}

void returns_values_via_future() {
    ThreadPool pool(2);
    auto f1 = pool.submit([] { return 21 * 2; });
    auto f2 = pool.submit([](int a, int b) { return a + b; }, 3, 4);
    auto f3 = pool.submit([] { return std::string("hello"); });
    CHECK(f1.get() == 42);
    CHECK(f2.get() == 7);
    CHECK(f3.get() == "hello");
}

void propagates_exceptions() {
    ThreadPool pool(2);
    auto f = pool.submit([]() -> int { throw std::logic_error("boom"); });
    bool caught = false;
    try {
        f.get();
    } catch (const std::logic_error& e) {
        caught = std::string(e.what()) == "boom";
    }
    CHECK(caught);
    // The pool must survive a throwing task.
    CHECK(pool.submit([] { return 1; }).get() == 1);
}

void uses_multiple_threads() {
    ThreadPool pool(4);
    std::mutex m;
    std::set<std::thread::id> ids;
    std::vector<std::future<void>> fs;
    for (int i = 0; i < 64; ++i) {
        fs.push_back(pool.submit([&] {
            std::this_thread::sleep_for(5ms);
            std::lock_guard<std::mutex> lk(m);
            ids.insert(std::this_thread::get_id());
        }));
    }
    for (auto& f : fs) f.get();
    CHECK(ids.size() > 1);
    CHECK(ids.find(std::this_thread::get_id()) == ids.end());  // not the caller
}

void thread_count_default_and_explicit() {
    ThreadPool a(3);
    CHECK(a.thread_count() == 3);
    ThreadPool b;  // 0 => hardware_concurrency, at least 1
    CHECK(b.thread_count() >= 1);
}

void destructor_drains_queue() {
    std::atomic<int> done{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 200; ++i) {
            pool.submit([&] {
                std::this_thread::sleep_for(1ms);
                done.fetch_add(1);
            });
        }
        // No wait_idle(): destructor alone must finish all 200.
    }
    CHECK(done.load() == 200);
}

void submit_during_shutdown_throws() {
    // A task that keeps the pool busy while another thread destroys it is racy to
    // test directly, so we check the simpler invariant: after the destructor
    // has begun, submit() must not silently drop work. We emulate this with a
    // pool we shut down via a scope and a raw pointer.
    auto* pool = new ThreadPool(1);
    std::atomic<bool> threw{false};
    std::atomic<bool> go{false};
    auto blocker = pool->submit([&] {
        while (!go.load()) std::this_thread::sleep_for(1ms);
    });
    std::thread killer([&] { delete pool; });
    std::this_thread::sleep_for(50ms);  // let the destructor set stopping_
    try {
        // `pool` is mid-destruction; this probes the stopping_ path.
        // (Calling a member on an object being destroyed is only acceptable in
        // this controlled test: the destructor is blocked joining the worker.)
        pool->submit([] {});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    go = true;
    killer.join();
    blocker.get();
    CHECK(threw.load());
}

void many_producers() {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 10000;
    std::atomic<long> sum{0};
    std::atomic<bool> producer_failed{false};
    {
        ThreadPool pool(4);
        std::vector<std::thread> producers;
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&] {
                // An exception escaping a std::thread calls std::terminate, so catch here.
                try {
                    for (int i = 1; i <= kPerProducer; ++i)
                        pool.submit([&sum, i] { sum.fetch_add(i); });
                } catch (...) {
                    producer_failed = true;
                }
            });
        }
        for (auto& t : producers) t.join();
        CHECK(!producer_failed.load());
        pool.wait_idle();
    }
    const long expected = static_cast<long>(kProducers) * kPerProducer * (kPerProducer + 1) / 2;
    CHECK(sum.load() == expected);
}

void wait_idle_with_no_work_returns() {
    ThreadPool pool(2);
    pool.wait_idle();  // must not hang
    pool.submit([] {}).get();
    pool.wait_idle();
}

struct TestCase {
    const char* name;
    void (*fn)();
};

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // keep results visible even if a test crashes
    const TestCase tests[] = {
        {"runs_all_tasks", runs_all_tasks},
        {"returns_values_via_future", returns_values_via_future},
        {"propagates_exceptions", propagates_exceptions},
        {"uses_multiple_threads", uses_multiple_threads},
        {"thread_count_default_and_explicit", thread_count_default_and_explicit},
        {"destructor_drains_queue", destructor_drains_queue},
        {"submit_during_shutdown_throws", submit_during_shutdown_throws},
        {"many_producers", many_producers},
        {"wait_idle_with_no_work_returns", wait_idle_with_no_work_returns},
    };

    int failed = 0;
    for (const auto& t : tests) {
        try {
            with_timeout(20s, t.fn);
            std::printf("[ PASS ] %s\n", t.name);
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[ FAIL ] %s\n         %s\n", t.name, e.what());
        }
    }
    std::printf("\n%d/%zu passed\n", static_cast<int>(sizeof(tests) / sizeof(tests[0])) - failed,
                sizeof(tests) / sizeof(tests[0]));
    return failed == 0 ? 0 : 1;
}
