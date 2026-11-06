// Throughput benchmark. Prints a CSV you can paste into the README or plot.
//
//   ./bench_pool > results.csv
//
// Two workloads:
//   tiny : an almost-empty task. Measures queue/lock overhead (the pool's own cost).
//   cpu  : a ~50 microsecond CPU-bound task. Measures scaling with thread count.
// Baselines: a plain serial loop, and std::async per task.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

#include "threadpool/thread_pool.hpp"

using Clock = std::chrono::steady_clock;

static volatile unsigned long g_sink;

// `seed` differs per task so the compiler cannot hoist identical work out of the loop.
static unsigned long burn(unsigned long seed, unsigned iters) {
    unsigned long x = 88172645463325252UL ^ (seed * 0x9E3779B97F4A7C15UL);
    for (unsigned i = 0; i < iters; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    return x;
}

template <class F>
static double seconds(F&& f) {
    auto t0 = Clock::now();
    f();
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static double best_of(int reps, const std::function<double()>& run) {
    double best = 1e30;
    for (int i = 0; i < reps; ++i) best = std::min(best, run());
    return best;
}

int main() {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::vector<unsigned> thread_counts{1, 2, 4, 8};
    if (std::find(thread_counts.begin(), thread_counts.end(), hw) == thread_counts.end())
        thread_counts.push_back(hw);
    std::sort(thread_counts.begin(), thread_counts.end());

    struct Workload {
        const char* name;
        unsigned iters;
        int tasks;
    };
    const Workload workloads[] = {{"tiny", 1, 200000}, {"cpu", 20000, 20000}};

    std::printf("workload,impl,threads,tasks,seconds,tasks_per_sec\n");

    for (const auto& w : workloads) {
        // Serial baseline.
        double s = best_of(3, [&] {
            return seconds([&] {
                unsigned long acc = 0;
                for (int i = 0; i < w.tasks; ++i) acc += burn(i, w.iters);
                g_sink = acc;
            });
        });
        std::printf("%s,serial,1,%d,%.6f,%.0f\n", w.name, w.tasks, s, w.tasks / s);

        // std::async baseline (thread per task; only sane for the cpu workload).
        if (w.iters > 1) {
            const int n = std::min(w.tasks, 4000);
            double a = best_of(2, [&] {
                return seconds([&] {
                    std::vector<std::future<unsigned long>> fs;
                    fs.reserve(n);
                    for (int i = 0; i < n; ++i)
                        fs.push_back(std::async(std::launch::async, burn,
                                                static_cast<unsigned long>(i), w.iters));
                    unsigned long acc = 0;
                    for (auto& f : fs) acc += f.get();
                    g_sink = acc;
                });
            });
            std::printf("%s,std_async,-,%d,%.6f,%.0f\n", w.name, n, a, n / a);
        }

        // Your pool.
        for (unsigned threads : thread_counts) {
            double p = best_of(3, [&] {
                tp::ThreadPool pool(threads);
                return seconds([&] {
                    std::vector<std::future<unsigned long>> fs;
                    fs.reserve(w.tasks);
                    for (int i = 0; i < w.tasks; ++i)
                        fs.push_back(pool.submit(burn, static_cast<unsigned long>(i), w.iters));
                    unsigned long acc = 0;
                    for (auto& f : fs) acc += f.get();
                    g_sink = acc;
                });
            });
            std::printf("%s,thread_pool,%u,%d,%.6f,%.0f\n", w.name, threads, w.tasks, p,
                        w.tasks / p);
        }
    }
    return 0;
}
