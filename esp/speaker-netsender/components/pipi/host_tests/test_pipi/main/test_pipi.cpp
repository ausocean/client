#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"

#include "gtest/gtest.h"
#include "pipi.hpp"

namespace Pipi {
// Forward declarations
void assert_log_level(const int fd, const char *level);

// Write a log to the given logger in the ESP log format:
// "<level> (hh:mm:ss.mmm) tag: <message>\n"
esp_err_t log_esp_msg(FileLogger &logger, const char level, const char *message)
{
    char msg[512];
    snprintf(msg, sizeof(msg), "%c (00:00:00.000) tag: %s\n", level, message);
    return logger.log(msg);
}

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

    int fd = ::open("entry.log", O_CREAT | O_RDWR | O_TRUNC, 0666);
    ASSERT_GE(fd, 0);

    ASSERT_EQ(entry.write(fd), ESP_OK);

    char got[Pipi::Entry::MAX_LOG_LENGTH + 100];
    ASSERT_NE(::lseek(fd, 0, SEEK_SET), -1);
    auto len = ::read(fd, got, sizeof(got) - 1);
    ASSERT_GT(len, 0);
    got[len] = '\0';

    char want[Pipi::Entry::MAX_LOG_LENGTH + 100];
    snprintf(want, sizeof(want),
             "{\"caller\":\"speaker-netsender\",\"timestamp\":%" PRId64 ",\"level\":\"info\",\"message\":\"%s\"}\n",
             static_cast<int64_t>(this->TEST_TIME), msg);

    EXPECT_STREQ(want, got);
    ::close(fd);
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
        std::filesystem::remove_all(TEST_DIR);
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
    ASSERT_TRUE(p.ready); // FileLogger is ready.

    struct stat buffer;
    stat(LOG_DIR, &buffer);
    ASSERT_TRUE(S_ISDIR(buffer.st_mode)); // Log Dir was created.
}

TEST_F(TestFileLogger, Write_Logs_Direct)
{
    constexpr auto LOG_DIR = "write_logs_direct";
    auto p = FileLogger(LOG_DIR);

    auto err = log_esp_msg(p, 'I', "This is an INFO log");
    ASSERT_EQ(err, ESP_OK);
    err = log_esp_msg(p, 'I', "This is a second INFO log");
    ASSERT_EQ(err, ESP_OK);
    err = log_esp_msg(p, 'W', "This is a WARN log");
    ASSERT_EQ(err, ESP_OK);
    err = log_esp_msg(p, 'E', "This is an ERROR log");
    ASSERT_EQ(err, ESP_OK);
    err = log_esp_msg(p, 'F', "This is a FATAL log");
    ASSERT_EQ(err, ESP_OK);

    // Create a new logfile so we can read from the old one.
    auto logs_fd = p.get_logs();
    ASSERT_GE(logs_fd, 0);
    ASSERT_NE(::lseek(logs_fd, 0, SEEK_SET), -1);

    // Assert the log levels.
    assert_log_level(logs_fd, "info");
    assert_log_level(logs_fd, "info");
    assert_log_level(logs_fd, "warn");
    assert_log_level(logs_fd, "error");
    assert_log_level(logs_fd, "fatal");
}

// Read the next line from the log file and assert it contains the given level string.
void assert_log_level(const int fd, const char *level)
{
    constexpr auto SEARCH_LEN = 1024;
    char line[SEARCH_LEN];
    size_t i = 0;
    char c;
    while (::read(fd, &c, 1) == 1 && c != '\n' && i < sizeof(line) - 1) {
        line[i++] = c;
    }
    line[i] = '\0';
    ASSERT_GT(i, 0);

    char want[32];
    snprintf(want, sizeof(want), "\"level\":\"%s\"", level);
    ASSERT_NE(strstr(line, want), nullptr) << "line: " << line;
}
} // namespace Pipi
