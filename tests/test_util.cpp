#include <gtest/gtest.h>
#include "common/util.h"

using namespace pc;

TEST(ParseDuration, BareSeconds) {
    EXPECT_EQ(parse_duration("30"), 30);
    EXPECT_EQ(parse_duration("0"), 0);
    EXPECT_EQ(parse_duration("1"), 1);
    EXPECT_EQ(parse_duration("3600"), 3600);
}

TEST(ParseDuration, SecondsSuffix) {
    EXPECT_EQ(parse_duration("30s"), 30);
    EXPECT_EQ(parse_duration("0s"), 0);
    EXPECT_EQ(parse_duration("90s"), 90);
}

TEST(ParseDuration, MinutesSuffix) {
    EXPECT_EQ(parse_duration("1m"), 60);
    EXPECT_EQ(parse_duration("5m"), 300);
    EXPECT_EQ(parse_duration("90m"), 5400);
}

TEST(ParseDuration, HoursSuffix) {
    EXPECT_EQ(parse_duration("1h"), 3600);
    EXPECT_EQ(parse_duration("2h"), 7200);
    EXPECT_EQ(parse_duration("12h"), 43200);
}

TEST(ParseDuration, Empty) {
    EXPECT_EQ(parse_duration(""), 0);
}
