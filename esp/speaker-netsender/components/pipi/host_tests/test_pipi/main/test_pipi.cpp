#include <sstream>

#include "esp_log.h"

#include "gtest/gtest.h"
#include "pipi.hpp"

namespace Pipi {

class TestEntry : public testing::Test {
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

} // namespace Pipi
