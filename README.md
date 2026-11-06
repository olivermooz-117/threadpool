# threadpool

A fixed-size C++17 thread pool with futures, built from scratch (mutex + condition variable), tested under ThreadSanitizer and benchmarked against a serial loop and `std::async`.

> Status: complete. 9/9 tests pass, and 100 consecutive ThreadSanitizer runs report no data races.

## API

```cpp
tp::ThreadPool pool(4);                        // 0 => hardware_concurrency()
auto fut = pool.submit([](int a, int b) { return a + b; }, 3, 4);
int sum = fut.get();                           // 7; task exceptions rethrow here
pool.wait_idle();                              // block until queue empty and no task running
// ~ThreadPool() finishes all queued work, then joins the workers
```

`submit()` throws `std::runtime_error` if the pool is shutting down.

## Build and test

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure       # or ./build/test_thread_pool

# Race detection (do this before you trust any green result)
cmake -S . -B build-tsan -DSANITIZE=thread -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-tsan -j && ./build-tsan/test_thread_pool

# Memory errors / UB
cmake -S . -B build-asan -DSANITIZE=address && cmake --build build-asan -j && ./build-asan/test_thread_pool
```

## Benchmark

```bash
./build/bench_pool > results.csv
```

Columns: `workload, impl, threads, tasks, seconds, tasks_per_sec`.
`tiny` measures queue/lock overhead, `cpu` (~50 us tasks) measures scaling.

Machine: Intel Core i5-8265U (4 cores / 8 threads, 1.6 GHz base), Ubuntu, GCC 15.2, Release build, best of 3 runs.

**CPU-bound tasks (about 30 us each)**

| impl        | threads | tasks/sec | vs 1 thread |
|-------------|---------|-----------|-------------|
| serial      | 1       | 31,569    | -           |
| std::async  | -       | 37,738    | -           |
| thread_pool | 1       | 28,961    | 1.0x        |
| thread_pool | 2       | 57,045    | 1.97x       |
| thread_pool | 4       | 96,589    | 3.34x       |
| thread_pool | 8       | 169,988   | 5.87x       |

(`std::async` ran 4,000 tasks instead of 20,000, since spawning a thread per task is slow.)

**Tiny tasks (measures pool overhead)**

| threads | tasks/sec |
|---------|-----------|
| 1       | 838,023   |
| 2       | 853,526   |
| 4       | 681,844   |
| 8       | 453,894   |

**What the numbers show**
- CPU-bound work scales almost linearly up to the 4 physical cores. The 5.9x at 8 threads
  suggests hyperthreading fills pipeline gaps in this dependent-chain workload (hypothesis, not measured).
- The pool costs about 1.2 us per task with one thread, so tasks should be well above 10 us to pay off.
- With tiny tasks, adding threads makes throughput worse: all workers contend on one mutex and
  condition variable. This is the bottleneck that per-worker queues with work stealing would address.

## Milestones

1. **Works:** `submit` + worker loop, all tests except shutdown ones pass.
2. **Shuts down cleanly:** destructor drains the queue, joins, never hangs.
3. **Correct under TSan:** zero reports on every test, repeated 100x
   (`for i in $(seq 100); do ./build-tsan/test_thread_pool || break; done`).
4. **Benchmarked:** table above filled in, with a short note on what the numbers show.
5. **Stretch (pick one or two):**
   - Per-thread queues with work stealing, compare against the single-queue version.
   - Lock-free or bounded MPMC queue.
   - Task priorities, or `submit` with a cancellation token.
   - Replace `std::function` with a small-buffer-optimised move-only callable.

## Design notes (write these as you go, interviewers ask about them)

- Why `packaged_task` is held in a `shared_ptr`.
- Why tasks run with the mutex released.
- The exact condition under which a worker exits.
- What `wait_idle()` guarantees and what it does not (tasks submitted concurrently).
- Where the single-queue design bottlenecks and what the benchmark shows about it.
