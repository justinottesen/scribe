#include <scribe/util.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <source_location>
#include <thread>

using scribe::Record;

namespace {

auto make_record(int value) -> Record<int> {
    return {
        .loc     = std::source_location::current(),
        .time    = std::chrono::system_clock::now(),
        .tid     = std::this_thread::get_id(),
        .payload = value,
    };
}

// A void handler that increments a counter on each call.
struct CountingHandler {
    int* count;
    void handle(const Record<int>& /*unused*/) const { (*count)++; }
};

// A bool handler that increments a counter and returns a fixed result.
struct GatingHandler {
    int*  count;
    bool  result;
    [[nodiscard]] auto handle(const Record<int>& /*unused*/) const -> bool {
        (*count)++;
        return result;
    }
};

}    // namespace

// --- Concept compliance ---

static_assert(scribe::Handler<scribe::util::Chain<CountingHandler, CountingHandler>, int>);
static_assert(scribe::Handler<
    scribe::util::Filter<CountingHandler, decltype([](const Record<int>&) -> bool { return true; })>,
    int>);

// --- Chain ---

TEST(Chain, CallsAllVoidHandlers) {
    int a = 0;
    int b = 0;
    scribe::util::Chain chain{CountingHandler{&a}, CountingHandler{&b}};
    chain.handle(make_record(0));
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(Chain, ShortCircuitsWhenHandlerReturnsFalse) {
    int a = 0;
    int b = 0;
    scribe::util::Chain chain{GatingHandler{.count = &a, .result = false}, CountingHandler{&b}};
    chain.handle(make_record(0));
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 0);    // skipped due to short-circuit
}

TEST(Chain, ContinuesWhenHandlerReturnsTrue) {
    int a = 0;
    int b = 0;
    scribe::util::Chain chain{GatingHandler{.count = &a, .result = true}, CountingHandler{&b}};
    chain.handle(make_record(0));
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

// --- Filter ---

TEST(Filter, ForwardsWhenPredicateTrue) {
    int count = 0;
    scribe::util::Filter filter{CountingHandler{&count}, [](const Record<int>&) -> bool { return true; }};
    auto result = filter.handle(make_record(0));
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(result);
}

TEST(Filter, DropsAndReturnsFalseWhenPredicateFalse) {
    int count = 0;
    scribe::util::Filter filter{CountingHandler{&count}, [](const Record<int>&) -> bool { return false; }};
    auto result = filter.handle(make_record(0));
    EXPECT_EQ(count, 0);
    EXPECT_FALSE(result);
}

TEST(Filter, PredicateReceivesRecord) {
    int dummy   = 0;
    int seen    = -1;
    scribe::util::Filter filter{
        CountingHandler{&dummy},
        [&](const Record<int>& r) -> bool { seen = r.payload; return true; }
    };
    filter.handle(make_record(42));
    EXPECT_EQ(seen, 42);
}
