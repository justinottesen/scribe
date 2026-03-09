# scribe - WORK IN PROGRESS

A performant, parallel C++ logging library built on C++26.

## Design

### Concepts

**Logger** — The entry point for callers. Each logger is templated on a `Payload` type and a `Handler`, and owns an internal queue and a dedicated consumer thread. Callers push a record onto the queue and return immediately; the consumer thread handles everything from there. Multiple loggers can coexist in a single process — for example, one per subsystem or library.

**Handler** — Any type that can be called with an rvalue `Record<Payload>`. The logger holds a single handler instance and calls it for each record in order on the consumer thread. Composition (fan-out, filtering, chaining) is the handler's responsibility — the logger does not impose any structure.

**Record** — The unit of data delivered to a handler. It carries the user-provided payload alongside metadata captured at the call site: source location, wall-clock timestamp, and calling thread id.

### Threading model

```
[Thread A] ──┐
[Thread B] ──┼──► [Queue] ──► [Consumer Thread] ──► Handler
[Thread C] ──┘
```

- The hot path for callers is a single queue push — the caller is never blocked on handler execution.
- Records are delivered to the handler in the order they were enqueued.
- Parallelism across loggers is natural: each logger has its own consumer thread.
- On destruction, the consumer thread drains the queue before joining — no records are lost.
- An optional `Capacity` template parameter bounds the queue size. When the queue is full, `log()` blocks until the consumer makes space, providing backpressure. The default is unbounded.

### Example

```cpp
struct MyHandler {
    void handle(scribe::Record<std::string>&& r) {
        std::println("[{}] {}", r.tid, r.payload);
    }
};

int main() {
    scribe::Logger<std::string, MyHandler> logger{MyHandler{}};

    logger.log("hello from any thread");
}
```

To bound the queue and apply backpressure to producers:

```cpp
scribe::Logger<std::string, MyHandler, 1024> logger{MyHandler{}};
```

## Requirements

- C++26
- CMake 3.28+

## Building

```sh
cmake -B build
cmake --build build
```

Tests are built automatically when scribe is the top-level project. To disable:

```sh
cmake -B build -DSCRIBE_BUILD_TESTS=OFF
```

## Integration

scribe is a header-only library. Add it to your CMake project:

```cmake
add_subdirectory(scribe)
target_link_libraries(your_target PRIVATE scribe)
```
