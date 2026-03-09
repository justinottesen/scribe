#include <scribe/scribe.hpp>

#include <atomic>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

// Concept compliance
struct ValidHandler {
    void handle(scribe::Record<int> /*unused*/) {}
};

struct NoHandleMethod {};

struct WrongSignature {
    void handle(int /*unused*/) {}
};

static_assert(scribe::Handler<ValidHandler, int>);
static_assert(!scribe::Handler<NoHandleMethod, int>);
static_assert(!scribe::Handler<WrongSignature, int>);

// Collects payloads into a vector, counts down a latch per message.
template <typename Payload>
struct CollectHandler {
    std::vector<Payload>* out;
    std::mutex*           mtx;
    std::latch*           latch;

    void handle(scribe::Record<Payload> r) {
        {
            std::lock_guard lock(*mtx);
            out->push_back(std::move(r.payload));
        }
        latch->count_down();
    }
};

template <typename Payload>
auto make_logger(std::vector<Payload>& out, std::mutex& mtx, std::latch& latch) {
    return scribe::Logger<Payload, CollectHandler<Payload>>{
        CollectHandler<Payload>{&out, &mtx, &latch}
    };
}

}    // namespace

// Handler receives a logged message.
TEST(Logger, ReceivesMessage) {
    std::vector<std::string> received;
    std::mutex               mtx;
    std::latch               done{1};

    auto logger = make_logger(received, mtx, done);
    logger.log("hello");
    done.wait();

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], "hello");
}

// Messages from a single thread arrive in the order they were logged.
TEST(Logger, PreservesOrdering) {
    constexpr int    N = 100;
    std::vector<int> received;
    std::mutex       mtx;
    std::latch       done{N};

    auto logger = make_logger(received, mtx, done);
    for (int i = 0; i < N; i++) { logger.log(i); }
    done.wait();

    ASSERT_EQ(received.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; i++) { EXPECT_EQ(received[i], i); }
}

// All queued messages are processed before the logger is destroyed.
TEST(Logger, DrainsOnDestroy) {
    constexpr int    N = 1000;
    std::atomic<int> count{0};

    struct CountHandler {
        std::atomic<int>* count;

        void handle(scribe::Record<int> /*unused*/) const {
            count->fetch_add(1, std::memory_order_relaxed);
        }
    };

    {
        scribe::Logger<int, CountHandler> logger{CountHandler{&count}};
        for (int i = 0; i < N; i++) { logger.log(i); }
    }    // jthread joins here — consumer has finished

    EXPECT_EQ(count.load(), N);
}

// Record carries the correct payload, thread id, timestamp, and source location.
TEST(Logger, RecordMetadata) {
    struct CaptureHandler {
        scribe::Record<int>* out;
        std::latch*          latch;

        void handle(scribe::Record<int> r) const {
            *out = r;
            latch->count_down();
        }
    };

    scribe::Record<int> captured;
    std::latch          done{1};

    scribe::Logger<int, CaptureHandler> logger{
        CaptureHandler{.out = &captured, .latch = &done}
    };

    auto before     = std::chrono::system_clock::now();
    auto caller_tid = std::this_thread::get_id();
    logger.log(42);
    done.wait();
    auto after = std::chrono::system_clock::now();

    EXPECT_EQ(captured.payload, 42);
    EXPECT_EQ(captured.tid, caller_tid);
    EXPECT_GE(captured.time, before);
    EXPECT_LE(captured.time, after);
    EXPECT_NE(std::string_view{captured.loc.file_name()}.find("scribe.test"),
              std::string_view::npos);
}

// Messages from multiple producer threads are all delivered.
TEST(Logger, MultipleProducers) {
    constexpr int    THREADS    = 8;
    constexpr int    PER_THREAD = 50;
    constexpr int    TOTAL      = THREADS * PER_THREAD;
    std::vector<int> received;
    std::mutex       mtx;
    std::latch       done{TOTAL};

    auto logger = make_logger(received, mtx, done);

    {
        std::vector<std::jthread> producers;
        producers.reserve(THREADS);
        for (int t = 0; t < THREADS; t++) {
            producers.emplace_back([&, t] -> void {
                for (int i = 0; i < PER_THREAD; i++) { logger.log((t * PER_THREAD) + i); }
            });
        }
    }    // producers join here

    done.wait();
    EXPECT_EQ(received.size(), static_cast<std::size_t>(TOTAL));
}

// Logger works with move-only payload types.
TEST(Logger, MoveOnlyPayload) {
    struct UniquePtrHandler {
        std::vector<int>* out;
        std::latch*       latch;

        void handle(scribe::Record<std::unique_ptr<int>> r) const {
            out->push_back(*r.payload);
            latch->count_down();
        }
    };

    std::vector<int> received;
    std::latch       done{3};

    scribe::Logger<std::unique_ptr<int>, UniquePtrHandler> logger{
        UniquePtrHandler{.out = &received, .latch = &done}
    };
    logger.log(std::make_unique<int>(10));
    logger.log(std::make_unique<int>(20));
    logger.log(std::make_unique<int>(30));
    done.wait();

    ASSERT_EQ(received.size(), 3);
    EXPECT_EQ(received[0], 10);
    EXPECT_EQ(received[1], 20);
    EXPECT_EQ(received[2], 30);
}
