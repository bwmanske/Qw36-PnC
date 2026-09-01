#include <gtest/gtest.h>
#include "common/util.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#include <cerrno>
#endif

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

// ── is_localhost_host ────────────────────────────────────────────

TEST(IsLocalhostHost, LoopbackIPv4) {
    EXPECT_TRUE(is_localhost_host("127.0.0.1"));
    EXPECT_TRUE(is_localhost_host("127.0.0.255"));
    EXPECT_TRUE(is_localhost_host("127.5.6.7"));
    EXPECT_TRUE(is_localhost_host("127.255.255.255"));
}

TEST(IsLocalhostHost, LoopbackIPv6) {
    EXPECT_TRUE(is_localhost_host("::1"));
}

TEST(IsLocalhostHost, LocalhostName) {
    EXPECT_TRUE(is_localhost_host("localhost"));
    EXPECT_TRUE(is_localhost_host("LOCALHOST"));
    EXPECT_TRUE(is_localhost_host("Localhost"));
}

TEST(IsLocalhostHost, NonLocal) {
    EXPECT_FALSE(is_localhost_host("192.168.1.5"));
    EXPECT_FALSE(is_localhost_host("10.0.0.1"));
    EXPECT_FALSE(is_localhost_host("8.8.8.8"));
    EXPECT_FALSE(is_localhost_host("example.com"));
    EXPECT_FALSE(is_localhost_host(""));
    EXPECT_FALSE(is_localhost_host("::2"));
}

TEST(IsLocalhostHost, Malformed) {
    EXPECT_FALSE(is_localhost_host("127."));
    EXPECT_FALSE(is_localhost_host("127.0.0"));
    EXPECT_FALSE(is_localhost_host("127.0.0.1.5"));
    EXPECT_FALSE(is_localhost_host("127.0.0.256"));
    EXPECT_FALSE(is_localhost_host("127.abc.def"));
    EXPECT_FALSE(is_localhost_host("127..0.1"));
}

// ── set_process_priority_below_normal ────────────────────────────

TEST(ProcessPriority, SetBelowNormal) {
#ifdef _WIN32
    DWORD original = GetPriorityClass(GetCurrentProcess());
    ASSERT_NE(original, 0);
    if (!set_process_priority_below_normal()) {
        GTEST_SKIP() << "SetPriorityClass unavailable";
    }
    EXPECT_EQ(GetPriorityClass(GetCurrentProcess()), BELOW_NORMAL_PRIORITY_CLASS);
    SetPriorityClass(GetCurrentProcess(), original); // restore
#else
    errno = 0;
    int original = getpriority(PRIO_PROCESS, 0);
    bool have_original = (original != -1) || (errno == 0);
    if (!set_process_priority_below_normal()) {
        GTEST_SKIP() << "setpriority unavailable";
    }
    int now = getpriority(PRIO_PROCESS, 0);
    EXPECT_GT(now, 0);
    setpriority(PRIO_PROCESS, 0, have_original ? original : 0); // restore
#endif
}
