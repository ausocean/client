#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <sstream>
#include <thread>
#include <unistd.h>

#include "esp_err.h"

#include "gtest/gtest.h"
#include "pipi.hpp"

namespace Pipi {
// Forward declarations
void assert_log_level(std::ifstream& f, int level);

class TestEntry : public testing::Test {
  protected:
    static constexpr auto TEST_DIR = "test";
    void SetUp() override
    {
        std::filesystem::create_directory(TEST_DIR);
        chdir(TEST_DIR);
    }
    void TearDown() override
    {
        chdir("../");
        std::filesystem::remove_all(TEST_DIR);
    }

  public:
    static constexpr auto TEST_TIME = 1064;
};

TEST_F(TestEntry, Create)
{
    time_t timer;
    time(&timer);

    constexpr auto msg = "This is a test log";

    auto entry = Entry(timer, Level::INFO, msg);

    ASSERT_EQ(entry.level, Level::INFO);
    ASSERT_EQ(entry.timestamp, timer);
    ASSERT_EQ(entry.data, msg);
}

TEST_F(TestEntry, Write)
{
    constexpr auto msg = "This is a test log";

    auto entry = Entry(this->TEST_TIME, Level::INFO, msg);

    std::stringstream out;
    entry.write(out);

    char got[Pipi::Entry::MAX_LOG_LENGTH + 100];
    out.get(got, Pipi::Entry::MAX_LOG_LENGTH + 100, '\0');

    char want[Pipi::Entry::MAX_LOG_LENGTH + 100];
    snprintf(want, Pipi::Entry::MAX_LOG_LENGTH + 100, "{\"timestamp\":%d,\"level\":%d,\"message\":\"%s\"}",
             this->TEST_TIME, Level::INFO, msg);

    EXPECT_STREQ(want, got);
}

class TestFileLogger : public testing::Test {
  protected:
    static constexpr auto TEST_DIR = "test";
    void SetUp() override
    {
        std::filesystem::create_directory(TEST_DIR);
        chdir(TEST_DIR);
    }
    void TearDown() override
    {
        chdir("../");
        // std::filesystem::remove_all(TEST_DIR);
    }
};

TEST_F(TestFileLogger, Create_Good_Path)
{
    constexpr auto LOG_DIR = "tmp";
    auto p = FileLogger(LOG_DIR);
    ASSERT_TRUE(p.ready);          // FileLogger is ready.
    ASSERT_STREQ(p.path, LOG_DIR); // Log path is as passed.
}

TEST_F(TestFileLogger, Create_With_No_Path)
{
    auto p = FileLogger();
    ASSERT_TRUE(p.ready);                     // FileLogger is ready.
    ASSERT_STREQ(p.path, Pipi::DEFAULT_PATH); // Log path is as passed.
}

TEST_F(TestFileLogger, Create_Empty_Path)
{
    constexpr auto LOG_DIR = "";
    auto p = FileLogger(LOG_DIR);
    ASSERT_TRUE(p.ready);                     // FileLogger is ready.
    ASSERT_STREQ(p.path, Pipi::DEFAULT_PATH); // Log path is as passed.
}

TEST_F(TestFileLogger, Make_Path)
{
    constexpr auto LOG_DIR = "long/path/to/logs";
    auto p = FileLogger(LOG_DIR);

    struct stat buffer;
    stat(LOG_DIR, &buffer);
    ASSERT_TRUE(S_ISDIR(buffer.st_mode)); // Log Dir was created.
}

TEST_F(TestFileLogger, Write_Logs_Direct)
{
    constexpr auto LOG_DIR = "write_logs_direct";
    auto p = FileLogger(LOG_DIR);

    constexpr auto TEST_INT = 1234;
    auto err = p.log(Level::INFO, "This is an INFO log with an int (%d)", TEST_INT);
    ASSERT_EQ(err, ESP_OK);
    err = p.log(Level::INFO, "This is an INFO log with no formatting");
    ASSERT_EQ(err, ESP_OK);
    err = p.log(Level::WARN, "This is a WARN log with an int (%d)", TEST_INT);
    ASSERT_EQ(err, ESP_OK);
    err = p.log(Level::WARN, "This is a WARN log with no formatting");
    ASSERT_EQ(err, ESP_OK);
    err = p.log(Level::ERROR, "This is an ERROR log with an int (%d)", TEST_INT);
    ASSERT_EQ(err, ESP_OK);
    err = p.log(Level::ERROR, "This is an ERROR log with no formatting");
    ASSERT_EQ(err, ESP_OK);
    err = p.log(Level::FATAL, "This is a FATAL log with an int (%d)", TEST_INT);
    ASSERT_EQ(err, ESP_OK);
    err = p.log(Level::FATAL, "This is a FATAL log with no formatting");
    ASSERT_EQ(err, ESP_OK);

    // This is required because logfiles are timestamped, and will overwrite itself if called too quickly.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // // Create a new logfile so we can read from the old one.
    p.new_file();
    ASSERT_TRUE(p.prev_file.is_open());

    // Assert the log levels.
    for (auto i = 0; i < 2; i++) {
        assert_log_level(p.prev_file, Level::INFO);
    }
    for (auto i = 0; i < 2; i++) {
        assert_log_level(p.prev_file, Level::WARN);
    }
    for (auto i = 0; i < 2; i++) {
        assert_log_level(p.prev_file, Level::ERROR);
    }
    for (auto i = 0; i < 2; i++) {
        assert_log_level(p.prev_file, Level::FATAL);
    }
}

void assert_log_level(std::ifstream& f, int level) {
    constexpr auto SEARCH_LEN = 1024;
    f.ignore(SEARCH_LEN, ','); // Skip over timestamp
    char got[SEARCH_LEN];
    f.getline(got, SEARCH_LEN, ',');

    char level_str[10];
    snprintf(level_str, 10, "\"level\":%d", level);
    ASSERT_STREQ(got, level_str);
}
} // namespace Pipi
