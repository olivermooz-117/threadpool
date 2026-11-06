# threadpool

A fixed-size C++17 thread pool with futures, built from scratch (mutex + condition variable), tested under ThreadSanitizer and benchmarked against a serial loop and `std::async`.

> Status: scaffold. The API, tests and benchmark are done; the concurrency core in
> `include/threadpool/thread_pool.hpp` is yours to write. Replace this note once it passes.

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

Results (fill in from your machine; include CPU model and core count):

| workload | impl        | threads | tasks/sec |
|----------|-------------|---------|-----------|
| cpu      | serial      | 1       | TODO      |
| cpu      | std::async  | -       | TODO      |
| cpu      | thread_pool | 4       | TODO      |

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
