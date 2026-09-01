#include <gtest/gtest.h>
#include "PWD_NextUnit.h"
#include <set>
#include <string>

// ── Lowercase-only tests ─────────────────────────────────────────

TEST(PWD_NextUnit, LowerAlpha_FirstChar) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_SUCCESS);
    EXPECT_STREQ(u.get_plainPassword(), "a");
}

TEST(PWD_NextUnit, LowerAlpha_Sequence) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // First few should be a, b, c, d, e
    for (char expected = 'a'; expected <= 'e'; expected++) {
        int ret = u.setNext();
        EXPECT_EQ(ret, PERMUTE_SUCCESS);
        EXPECT_STREQ(u.get_plainPassword(), std::string(1, expected).c_str());
    }
}

TEST(PWD_NextUnit, LowerAlpha_WrapsToTwoChars) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Exhaust 'a' through 'z' at length 1
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    // Next should be "aa"
    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_SUCCESS);
    EXPECT_STREQ(u.get_plainPassword(), "aa");
}

TEST(PWD_NextUnit, LowerAlpha_TwoCharProgression) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Exhaust single chars (a-z)
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    // Now at "aa", next should be "ab"
    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "aa");
    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "ab");
}

TEST(PWD_NextUnit, LowerAlpha_Done) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Total permutations: 26^1 + 26^2 + ... + 26^10 is huge.
    // Verify it produces valid results for many iterations without crashing.
    int successCount = 0;
    for (int i = 0; i < 1000; i++) {
        int ret = u.setNext();
        if (ret == PERMUTE_SUCCESS) {
            successCount++;
            char* pwd = u.get_plainPassword();
            ASSERT_NE(pwd, nullptr);
            // All chars should be lowercase a-z
            for (size_t j = 0; pwd[j]; j++) {
                EXPECT_GE(pwd[j], 'a');
                EXPECT_LE(pwd[j], 'z');
            }
        }
    }
    EXPECT_EQ(successCount, 1000);
}

// ── Multi-character-set tests ─────────────────────────────────────

TEST(PWD_NextUnit, AllSets_FirstChar) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);
    u.set_useUAlpha(true);
    u.set_useNumeric(true);
    u.set_useNAlpha(true);

    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_SUCCESS);
    EXPECT_STREQ(u.get_plainPassword(), "a");
}

TEST(PWD_NextUnit, NumericOnly) {
    PWD_NextUnit u;
    u.set_useNumeric(true);

    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_SUCCESS);
    EXPECT_STREQ(u.get_plainPassword(), "0");

    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "1");
}

TEST(PWD_NextUnit, NonAlphaOnly) {
    PWD_NextUnit u;
    u.set_useNAlpha(true);

    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_SUCCESS);
    EXPECT_STREQ(u.get_plainPassword(), "~");
}

TEST(PWD_NextUnit, NoOptions_ReturnsError) {
    PWD_NextUnit u;
    // Don't enable any character set

    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_NO_OPTION);
}

TEST(PWD_NextUnit, UpperAndLower_Transition) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);
    u.set_useUAlpha(true);

    // Exhaust lowercase at position 0 (a-z)
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    // Next should be 'A'
    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_SUCCESS);
    EXPECT_STREQ(u.get_plainPassword(), "A");
}

// ── get_pwdAsIndicies / get_pwdAsText tests ───────────────────────

TEST(PWD_NextUnit, pwdAsIndicies_Format) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    u.setNext(); // "a"
    std::string indicies = u.get_pwdAsIndicies();
    // Format: "len,idx0" → "1,0" for 'a'
    EXPECT_EQ(indicies, "1,0");
}

TEST(PWD_NextUnit, pwdAsText_ContainsChar) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);
    u.set_useNumeric(true);

    u.setNext(); // "a"
    std::string text = u.get_pwdAsText();
    EXPECT_NE(text.find('a'), std::string::npos);
}

// ── get_plainPassword null safety ─────────────────────────────────

TEST(PWD_NextUnit, PlainPassword_NotNull) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    u.setNext();
    EXPECT_NE(u.get_plainPassword(), nullptr);
}

// ── Password length tracking ──────────────────────────────────────

TEST(PWD_NextUnit, PasswordLength_Increases) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // After exhausting single chars, length should be 2
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    u.setNext(); // "aa"
    EXPECT_EQ(u.get_testPwdLen(), 2);
}

// ── Caret character (^) included in nonAlpha ──────────────────────

TEST(PWD_NextUnit, NonAlpha_ContainsCaret) {
    PWD_NextUnit u;
    u.set_useNAlpha(true);

    // Iterate through all non-alpha chars to find ^
    bool found = false;
    for (int i = 0; i < NA_COUNT + 10; i++) {
        int ret = u.setNext();
        if (ret != PERMUTE_SUCCESS) break;
        char* pwd = u.get_plainPassword();
        if (pwd && pwd[0] == '^') {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ── Checkpoint resume via charIndicies ────────────────────────────

TEST(PWD_NextUnit, ResumeViaIndicies) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);
    u.set_useUAlpha(true);

    // Advance to "c" (index 2)
    u.setNext(); // a
    u.setNext(); // b
    u.setNext(); // c

    // Save state
    std::string savedIndicies = u.get_pwdAsIndicies();
    int savedLen = u.get_testPwdLen();

    // Create new generator and restore
    PWD_NextUnit u2;
    u2.set_useLAlpha(true);
    u2.set_useUAlpha(true);
    u2.set_testPwdLen(savedLen);

    // Parse indicies string "1,2" → len=1, idx[0]=2
    // Set charIndicies manually
    u2.set_charIndicies(0, 2);

    // Next should be "d" (index 3)
    u2.setNext();
    EXPECT_STREQ(u2.get_plainPassword(), "d");
}

// ── Reverse ordering tests (index 0 = rightmost char) ─────────────

TEST(PWD_NextUnit, TwoChar_RightmostChangesFastest) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Exhaust single chars (a-z), then step through two-char sequence.
    // Index 0 (rightmost in readable string) changes fastest:
    // "aa", "ab", "ac", "ad", ...
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }

    // First two-char: "aa"
    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "aa");

    // Rightmost increments: "ab", "ac", "ad"
    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "ab");
    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "ac");
    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "ad");
}

TEST(PWD_NextUnit, TwoChar_az_to_ba) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Exhaust single chars (a-z)
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    // Step through all 26 rightmost chars: aa, ab, ac, ... az
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    // Currently at "az"
    EXPECT_STREQ(u.get_plainPassword(), "az");
    // One more: rightmost wraps to 'a', leftmost increments → "ba"
    u.setNext();
    EXPECT_STREQ(u.get_plainPassword(), "ba");
}

TEST(PWD_NextUnit, TwoChar_FullCycle) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Exhaust single chars (a-z)
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    // Run through all 676 two-char permutations
    for (int i = 0; i < 676; i++) {
        int ret = u.setNext();
        EXPECT_EQ(ret, PERMUTE_SUCCESS);
    }
    // Last two-char should be "zz"
    EXPECT_STREQ(u.get_plainPassword(), "zz");
    // Next should be three-char "aaa"
    int ret = u.setNext();
    EXPECT_EQ(ret, PERMUTE_SUCCESS);
    EXPECT_STREQ(u.get_plainPassword(), "aaa");
}

TEST(PWD_NextUnit, Indicies_Match_Reversed_String) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Advance to a known two-char password
    for (int i = 0; i < 26; i++) {
        u.setNext();
    }
    // Step to "ad" (rightmost = 'd'=3, leftmost = 'a'=0)
    for (int i = 0; i < 4; i++) {
        u.setNext();
    }
    // At this point: "aa"(0), "ab"(1), "ac"(2), "ad"(3)
    EXPECT_STREQ(u.get_plainPassword(), "ad");

    // get_pwdAsIndicies iterates from high index down: "len,idx[1],idx[0]"
    std::string idxStr = u.get_pwdAsIndicies();
    // charIndicies[1]=0 ('a'), charIndicies[0]=3 ('d') → "2,0,3"
    EXPECT_EQ(idxStr, "2,0,3");
}

// ── Checkpoint accessor tests (get_charIndicies / permuteStatus) ──

TEST(PWD_NextUnit, GetCharIndicies_RoundTrip) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Default: all -1
    for (int i = 0; i < MAX_PWD_LEN; i++) {
        EXPECT_EQ(u.get_charIndicies(i), -1);
    }

    u.set_charIndicies(0, 5);
    u.set_charIndicies(3, 9);
    EXPECT_EQ(u.get_charIndicies(0), 5);
    EXPECT_EQ(u.get_charIndicies(3), 9);
    EXPECT_EQ(u.get_charIndicies(1), -1); // untouched
}

TEST(PWD_NextUnit, PermuteStatus_DefaultSuccess) {
    PWD_NextUnit u;
    EXPECT_EQ(u.get_permuteStatus(), PERMUTE_SUCCESS);
}

TEST(PWD_NextUnit, PermuteStatus_SetGet) {
    PWD_NextUnit u;
    u.set_permuteStatus(PERMUTE_DONE);
    EXPECT_EQ(u.get_permuteStatus(), PERMUTE_DONE);
    u.set_permuteStatus(PERMUTE_SUCCESS);
    EXPECT_EQ(u.get_permuteStatus(), PERMUTE_SUCCESS);
}

// ── Full checkpoint round-trip (all 10 indicies + len + status) ───

TEST(PWD_NextUnit, CheckpointRoundTrip_FullState) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);
    u.set_useUAlpha(true);

    // Advance past all 52 single chars into two-char territory (multi-digit)
    for (int i = 0; i < 100; i++) {
        u.setNext();
    }

    // Capture full state
    int savedLen = u.get_testPwdLen();
    int savedStatus = u.get_permuteStatus();
    int savedIndicies[MAX_PWD_LEN];
    for (int i = 0; i < MAX_PWD_LEN; i++) {
        savedIndicies[i] = u.get_charIndicies(i);
    }

    // The next password the original would produce
    int ret = u.setNext();
    ASSERT_EQ(ret, PERMUTE_SUCCESS);
    std::string expected = u.get_plainPassword();

    // Restore into a fresh generator
    PWD_NextUnit u2;
    u2.set_useLAlpha(true);
    u2.set_useUAlpha(true);
    u2.set_testPwdLen(savedLen);
    u2.set_permuteStatus(savedStatus);
    for (int i = 0; i < MAX_PWD_LEN; i++) {
        u2.set_charIndicies(i, savedIndicies[i]);
    }

    // u2's next should match the original's next
    int ret2 = u2.setNext();
    ASSERT_EQ(ret2, PERMUTE_SUCCESS);
    EXPECT_STREQ(u2.get_plainPassword(), expected.c_str());
}

TEST(PWD_NextUnit, CheckpointRoundTrip_DoneStaysDone) {
    PWD_NextUnit u;
    u.set_useLAlpha(true);

    // Simulate an exhausted generator
    u.set_permuteStatus(PERMUTE_DONE);

    int savedStatus = u.get_permuteStatus();
    int savedLen = u.get_testPwdLen();
    int savedIndicies[MAX_PWD_LEN];
    for (int i = 0; i < MAX_PWD_LEN; i++) {
        savedIndicies[i] = u.get_charIndicies(i);
    }

    PWD_NextUnit u2;
    u2.set_useLAlpha(true);
    u2.set_testPwdLen(savedLen);
    u2.set_permuteStatus(savedStatus);
    for (int i = 0; i < MAX_PWD_LEN; i++) {
        u2.set_charIndicies(i, savedIndicies[i]);
    }

    // Restored DONE generator should immediately report done
    EXPECT_EQ(u2.setNext(), PERMUTE_DONE);
}
