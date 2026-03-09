#pragma once

/**
 * Scribe is a simple, performant logging library for multi-threaded C++ programs.
 *
 * Callers on any thread push records into a Logger. A dedicated consumer thread drains the
 * queue and dispatches records to a user-provided Handler, keeping the hot path minimal and
 * the caller unblocked.
 */

#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <queue>
#include <source_location>
#include <thread>

namespace scribe {

/**
 * A Record bundles a user-provided Payload with metadata captured at the log call site:
 * the source location, wall-clock timestamp, and calling thread id.
 */
template <typename Payload>
struct Record {
    std::source_location                  loc;
    std::chrono::system_clock::time_point time;
    std::thread::id                       tid;
    Payload                               payload;
};

/**
 * A Handler is any type that can be called with an rvalue Record<Payload>. The return type
 * is unconstrained — handlers may return void or any other type.
 */
template <typename H, typename Payload>
concept Handler = requires(H h) { h.handle(std::declval<Record<Payload>>()); };

/**
 * Logger accepts records from any number of producer threads and dispatches them
 * sequentially to a Handler on a private consumer thread.
 *
 * The log() call returns as soon as the record is on the queue — the caller is never
 * blocked on handler execution. Records are delivered to the handler in the order they
 * were enqueued. On destruction, the consumer thread drains the queue before joining,
 * so no records are lost.
 *
 * Capacity sets the maximum number of records the queue may hold. When the queue is full,
 * log() blocks until the consumer makes space. The default is unbounded.
 */
template <std::movable Payload, Handler<Payload> H,
          std::size_t Capacity = std::numeric_limits<std::size_t>::max()>
class Logger {
    static constexpr bool bounded = Capacity != std::numeric_limits<std::size_t>::max();

    struct NoCV {};

public:
    explicit Logger(H handler)
        : m_handler(std::move(handler))
        , m_consumer(std::bind_front(&Logger::consume, this)) {}

    ~Logger() = default;

    Logger(const Logger&)                    = delete;
    auto operator=(const Logger&) -> Logger& = delete;
    Logger(Logger&&)                         = delete;
    auto operator=(Logger&&) -> Logger&      = delete;

    auto log(Payload payload, std::source_location loc = std::source_location::current()) -> void {
        {
            std::unique_lock lock(m_mutex);
            if constexpr (bounded) {
                m_not_full.wait(lock, [this] -> auto { return m_queue.size() < Capacity; });
            }
            m_queue.push(Record<Payload>{
                .loc     = loc,
                .time    = std::chrono::system_clock::now(),
                .tid     = std::this_thread::get_id(),
                .payload = std::move(payload),
            });
        }
        m_cv.notify_one();
    }

private:
    H                                                                                m_handler;
    std::queue<Record<Payload>>                                                      m_queue;
    std::mutex                                                                       m_mutex;
    std::condition_variable_any                                                      m_cv;
    [[no_unique_address]] std::conditional_t<bounded, std::condition_variable, NoCV> m_not_full;
    std::jthread                                                                     m_consumer;

    auto consume(std::stop_token st) -> void {
        std::unique_lock lock(m_mutex);
        while (m_cv.wait(lock, st, [this] -> auto { return !m_queue.empty(); })) {
            auto record = std::move(m_queue.front());
            m_queue.pop();
            if constexpr (bounded) { m_not_full.notify_one(); }
            lock.unlock();

            m_handler.handle(std::move(record));
            lock.lock();
        }
    }
};

}    // namespace scribe
