#pragma once

// A fixed-size thread pool.
//
// YOUR JOB: replace every `TODO` below with a real implementation.
// The public API is fixed so the tests and benchmark compile as-is.
//
// Suggested order:
//   1. Get submit() + worker loop working with one mutex and one condition_variable.
//   2. Make the destructor drain the queue and join cleanly (no leaks, no hangs).
//   3. Make exceptions in tasks flow through the std::future.
//   4. Add wait_idle().
//   5. Run the tests under -DSANITIZE=thread until they are clean.
//   6. Only then optimise (see README "Stretch goals").

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace tp {

class ThreadPool {
public:
    // Start `num_threads` workers. If 0 is passed, use hardware_concurrency()
    // (and at least 1).
    explicit ThreadPool(std::size_t num_threads = 0) {
        // TODO: pick the thread count, then spawn workers into workers_.
        //   Each worker runs worker_loop().
        (void)num_threads;
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Must finish every task that was already submitted, then join all workers.
    ~ThreadPool() {
        // TODO: set stopping_ under the mutex, notify_all, join every worker.
        //   Think about: what if a worker is waiting on the condition variable
        //   right now? What if the queue is not empty when stopping_ is set?
    }

    // Queue a callable. Returns a future for its result.
    // Throws std::runtime_error if the pool is shutting down.
    template <class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        // TODO: wrap the call in a std::packaged_task<R()>.
        //   - std::packaged_task is move-only; std::function needs copyable, so
        //     hold it in a std::shared_ptr and capture that in a lambda.
        //   - Lock the mutex, throw if stopping_, push the task, unlock,
        //     then notify_one().
        //   - Return the packaged_task's future.
        (void)f;
        ((void)args, ...);
        throw std::runtime_error("ThreadPool::submit not implemented");
        return std::future<R>{};
    }

    // Block until the queue is empty AND no worker is running a task.
    void wait_idle() {
        // TODO: you need a second condition variable (or a clever use of one)
        //   and a counter of tasks currently executing.
        throw std::runtime_error("ThreadPool::wait_idle not implemented");
    }

    std::size_t thread_count() const noexcept { return workers_.size(); }

private:
    void worker_loop() {
        // TODO: loop { wait for task or stopping_; pop; unlock; run; }
        //   Exit only when stopping_ is set AND the queue is empty.
        //   Never run a task while holding the mutex.
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
    // TODO: add whatever you need for wait_idle() (e.g. active_ counter, idle_cv_).
};

}  // namespace tp
