#include <scribe/defaults.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <scribe/util.hpp>

using namespace scribe::defaults;
using scribe::Record;

// --- Concept compliance ---

static_assert(scribe::Handler<ConsoleHandler, Message>);
static_assert(scribe::Handler<FileHandler, Message>);
static_assert(scribe::Handler<LevelFilter, Message>);

namespace {

auto make_record(Message payload) -> Record<Message> {
    return {
        .loc     = std::source_location::current(),
        .time    = std::chrono::system_clock::now(),
        .tid     = std::this_thread::get_id(),
        .payload = std::move(payload),
    };
}

auto read_file(const std::filesystem::path& path) -> std::string {
    std::ifstream f{path};
    return {std::istreambuf_iterator<char>{f}, {}};
}

// Used by the LevelFilter composition test.
struct CountingHandler {
    int* count;

    void handle(const Record<Message>& /*unused*/) const { (*count)++; }
};

}    // namespace

// --- Message ---

TEST(Message, DefaultConstruction) {
    Message m;
    EXPECT_EQ(m.level, Level::Info);
    EXPECT_TRUE(m.text.empty());
}

TEST(Message, FormatsOnConstruction) {
    Message m{Level::Warn, "value is {}", 42};
    EXPECT_EQ(m.level, Level::Warn);
    EXPECT_EQ(m.text, "value is 42");
}

// --- detail::levelString ---

TEST(LevelString, MapsAllLevels) {
    EXPECT_EQ(detail::levelString(Level::Trace), "TRACE");
    EXPECT_EQ(detail::levelString(Level::Debug), "DEBUG");
    EXPECT_EQ(detail::levelString(Level::Info), "INFO ");
    EXPECT_EQ(detail::levelString(Level::Warn), "WARN ");
    EXPECT_EQ(detail::levelString(Level::Error), "ERROR");
    EXPECT_EQ(detail::levelString(Level::Fatal), "FATAL");
}

// --- detail::formatRecord ---

TEST(FormatRecord, ContainsLevelAndMessage) {
    auto line = detail::formatRecord(make_record({Level::Error, "something went wrong"}));
    EXPECT_NE(line.find("ERROR"), std::string::npos);
    EXPECT_NE(line.find("something went wrong"), std::string::npos);
}

TEST(FormatRecord, LevelIsBracketed) {
    auto line = detail::formatRecord(make_record({Level::Info, "msg"}));
    EXPECT_NE(line.find("[INFO ]"), std::string::npos);
}

TEST(FormatRecord, TimestampPrecedesLevel) {
    auto line    = detail::formatRecord(make_record({Level::Debug, "msg"}));
    auto ts      = line.find('-');    // first '-' is from YYYY-MM-DD
    auto bracket = line.find('[');    // '[' opens the level field
    EXPECT_NE(ts, std::string::npos);
    EXPECT_NE(bracket, std::string::npos);
    EXPECT_LT(ts, bracket);
}

// --- ConsoleHandler ---

// Output goes to stdout; just verify it doesn't throw or crash.
TEST(ConsoleHandler, HandleDoesNotCrash) {
    EXPECT_NO_THROW(ConsoleHandler::handle(make_record({Level::Info, "console test"})));
}

// --- FileHandler ---

class FileHandlerTest : public testing::Test {
protected:
    std::filesystem::path m_path{std::filesystem::temp_directory_path()
                                 / "scribe_defaults_test.log"};

    void TearDown() override { std::filesystem::remove(m_path); }
};

TEST_F(FileHandlerTest, ThrowsOnInvalidPath) {
    EXPECT_THROW(FileHandler{"/nonexistent/path/scribe.log"}, std::system_error);
}

TEST_F(FileHandlerTest, WritesFormattedRecord) {
    FileHandler handler{m_path};
    handler.handle(make_record({Level::Info, "hello file"}));

    auto contents = read_file(m_path);
    EXPECT_NE(contents.find("[INFO ]"), std::string::npos);
    EXPECT_NE(contents.find("hello file"), std::string::npos);
}

TEST_F(FileHandlerTest, PreservesOrderAcrossMultipleRecords) {
    FileHandler handler{m_path};
    handler.handle(make_record({Level::Debug, "first"}));
    handler.handle(make_record({Level::Error, "second"}));

    auto contents   = read_file(m_path);
    auto pos_first  = contents.find("first");
    auto pos_second = contents.find("second");

    ASSERT_NE(pos_first, std::string::npos);
    ASSERT_NE(pos_second, std::string::npos);
    EXPECT_LT(pos_first, pos_second);
}

TEST_F(FileHandlerTest, AppendsAcrossInstances) {
    { FileHandler{m_path}.handle(make_record({Level::Info, "first session"})); }
    { FileHandler{m_path}.handle(make_record({Level::Info, "second session"})); }

    auto contents = read_file(m_path);
    EXPECT_NE(contents.find("first session"), std::string::npos);
    EXPECT_NE(contents.find("second session"), std::string::npos);
}

// --- LevelFilter ---

TEST(LevelFilter, PassesAtAndAboveMinLevel) {
    LevelFilter f{Level::Warn};
    EXPECT_TRUE(f.handle(make_record({Level::Warn, "at threshold"})));
    EXPECT_TRUE(f.handle(make_record({Level::Error, "above threshold"})));
    EXPECT_TRUE(f.handle(make_record({Level::Fatal, "above threshold"})));
}

TEST(LevelFilter, DropsBelow) {
    LevelFilter f{Level::Warn};
    EXPECT_FALSE(f.handle(make_record({Level::Trace, "below threshold"})));
    EXPECT_FALSE(f.handle(make_record({Level::Debug, "below threshold"})));
    EXPECT_FALSE(f.handle(make_record({Level::Info, "below threshold"})));
}

// LevelFilter short-circuits a Chain, preventing subsequent handlers from running.
TEST(LevelFilter, GatesSubsequentHandlersInChain) {
    int                 count = 0;
    scribe::util::Chain chain{LevelFilter{Level::Warn}, CountingHandler{&count}};

    chain.handle(make_record({Level::Info, "below — dropped"}));
    EXPECT_EQ(count, 0);

    chain.handle(make_record({Level::Error, "above — passes"}));
    EXPECT_EQ(count, 1);
}
