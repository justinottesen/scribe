# scribe - WORK IN PROGRESS

A performant, parallel C++ logging library built on C++26.

## Design

### Concepts

**Logger** — The entry point for callers. Each logger owns an internal lock-free MPSC queue and a dedicated consumer thread. Callers push a log record onto the queue and return immediately; the consumer thread handles everything from there. Multiple loggers can coexist in a single process — for example, one per subsystem or library.

**Handler** — Registered on a logger. When the consumer thread pulls a record off the queue, it passes it through the logger's handler chain sequentially. Handlers are responsible for filtering (e.g., by log level) and formatting. The chain always runs on the consumer thread, so handlers are never called concurrently for a given logger and require no internal synchronization.

**Sink** — The terminal output target. Handlers route formatted messages to one or more sinks. Sinks are responsible for the actual write (file, stdout, network, etc.). Because multiple loggers may share a sink, it is the sink's responsibility to support concurrent calls — either by being inherently thread-safe or by wrapping writes in a mutex.

### Threading model

```
[Thread A] ──┐
[Thread B] ──┼──► [Lock-free MPSC Queue] ──► [Consumer Thread] ──► Handlers ──► Sink(s)
[Thread C] ──┘
```

- The hot path for callers is a single queue push — no blocking, no lock contention.
- Ordering is preserved: the consumer thread processes records one at a time, in order.
- Parallelism across loggers is natural: each logger has its own consumer thread.
- Slow sinks (e.g., network) can be made async internally with their own queue and thread, so the consumer thread never stalls on I/O.

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
target_link_libraries(your_target PRIVATE scribe::headers)
```
