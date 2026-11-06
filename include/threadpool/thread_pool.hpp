#pragma once

#include <algorithm>
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
    explicit ThreadPool(std::size_t num_threads = 0) {
        if (num_threads == 0) {
            num_threads = std::max(1u, std::thread::hardware_concurrency());
        }
        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();                 // wake every sleeping worker
        for (auto& t : workers_) t.join();
    }

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        // packaged_task is move-only, std::function needs a copyable callable,
        // so the task lives in a shared_ptr that the lambda copies.
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<R> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) throw std::runtime_error("ThreadPool is shutting down");
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();                 // wake one worker
        return result;
    }

    // Block until the queue is empty AND no task is currently running.
    void wait_idle() {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_cv_.wait(lock, [this] { return tasks_.empty() && active_ == 0; });
    }

    std::size_t thread_count() const noexcept { return workers_.size(); }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (tasks_.empty()) return;   // stopping_ is set and nothing left to drain
                job = std::move(tasks_.front());
                tasks_.pop();
                ++active_;                    // counted as running before the lock is released
            }
            job();                            // run with the mutex released
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --active_;
                if (tasks_.empty() && active_ == 0) idle_cv_.notify_all();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;       // wakes workers
    std::condition_variable idle_cv_;  // wakes wait_idle() callers
    bool stopping_ = false;
    std::size_t active_ = 0;           // tasks currently executing
};

}  // namespace tp
