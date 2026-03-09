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

## Utilities

`<scribe/util.hpp>` provides handler composition utilities.

### `scribe::Chain<Hs...>`

Dispatches a record to each handler in sequence. If any handler returns `false` the chain short-circuits and subsequent handlers are skipped. Handlers returning `void` always continue.

```cpp
// Log to both console and file
scribe::Logger<Message, scribe::Chain<ConsoleHandler, FileHandler>> logger{
    scribe::Chain{ConsoleHandler{}, FileHandler{"app.log"}}
};
```

Handlers used inside a `Chain` must accept `const Record<Payload>&` — since the record is shared across all chain members it cannot be moved.

### `scribe::Filter<H, Pred>`

Wraps a handler with a predicate. Records are forwarded to `H` only when `Pred(record)` returns `true`. Returns `false` when filtered, allowing an enclosing `Chain` to short-circuit.

```cpp
// Only log records from a specific thread
scribe::Filter{ConsoleHandler{}, [tid = std::this_thread::get_id()](const auto& r) {
    return r.tid == tid;
}}
```

### Composing utilities

`Chain` and `Filter` compose freely since both satisfy the `Handler` concept:

```cpp
// Filter by level, then fan out to console and file
scribe::Chain{
    LevelFilter{Level::Warn},
    ConsoleHandler{},
    FileHandler{"app.log"},
}
```

---

## Defaults

`<scribe/defaults.hpp>` provides ready-to-use types for common logging needs.

### `scribe::defaults::Level`

```cpp
enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal };
```

### `scribe::defaults::Message`

A payload type that eagerly formats its text at the call site using `std::format`:

```cpp
scribe::defaults::Message{Level::Warn, "retrying in {}ms", delay}
```

Formatting happens on the producer thread, keeping the consumer handler fast.

### `scribe::defaults::ConsoleHandler`

Writes formatted records to stdout. A static mutex serialises output across all `ConsoleHandler` instances, preventing interleaved lines when multiple loggers share the console.

Output format: `2024-01-01 12:00:00 [INFO ] message text`

Timestamps are UTC.

### `scribe::defaults::FileHandler`

Writes formatted records to a file in append mode. The buffer is flushed after every record, so output is visible immediately (e.g. via `tail -f`). Throws `std::system_error` if the file cannot be opened.

### `scribe::defaults::LevelFilter`

A chain-compatible handler that short-circuits when the record's level is below `min_level`. Returns `bool`, so placing it at the front of a `Chain` gates all subsequent handlers.

```cpp
scribe::Chain{LevelFilter{Level::Warn}, ConsoleHandler{}}
```

### Using the defaults together

```cpp
#include <scribe/defaults.hpp>

using namespace scribe::defaults;

int main() {
    scribe::Logger<Message, ConsoleHandler> logger{ConsoleHandler{}};

    logger.log(Message{Level::Info, "started up"});
    logger.log(Message{Level::Warn, "retrying in {}ms", 500});
}
```

```cpp
// Log to a file with a bounded queue
scribe::Logger<Message, FileHandler, 1024> logger{FileHandler{"app.log"}};
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
