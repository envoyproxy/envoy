#include <vector>

#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cache/http_cache_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

// TODO(#9872): Add tests for eat* functions
// TODO(#9872): More tests for httpTime, effectiveMaxAge

class HttpTimeTest : public testing::TestWithParam<const char*> {};

const char* const ok_times[] = {
    "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate
    "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format
    "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format
};

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, testing::ValuesIn(ok_times));

TEST_P(HttpTimeTest, Ok) {
  Http::TestResponseHeaderMapImpl response_headers{{"date", GetParam()}};
  // Manually confirmed that 784111777 is 11/6/94, 8:46:37.
  EXPECT_EQ(784111777,
            SystemTime::clock::to_time_t(HttpCacheUtils::httpTime(response_headers.Date())));
}

TEST(HttpTime, Null) { EXPECT_EQ(HttpCacheUtils::httpTime(nullptr), SystemTime()); }

struct EffectiveMaxAgeParams {
  absl::string_view cache_control;
  int effective_max_age_secs;
};

EffectiveMaxAgeParams params[] = {
    {"public, max-age=3600", 3600},
    {"public, max-age=-1", 0},
    {"max-age=20", 20},
    {"max-age=86400, public", 86400},
    {"public,max-age=\"0\"", 0},
    {"public,max-age=8", 8},
    {"public,max-age=3,no-cache", 0},
    {"s-maxage=0", 0},
    {"max-age=10,s-maxage=0", 0},
    {"s-maxage=10", 10},
    {"no-cache", 0},
    {"max-age=0", 0},
    {"no-cache", 0},
    {"public", 0},
    // TODO(#9833): parse quoted forms
    // {"max-age=20, s-maxage=\"25\"",25},
    // {"public,max-age=\"8\",foo=11",8},
    // {"public,max-age=\"8\",bar=\"11\"",8},
    // TODO(#9833): parse public/private
    // {"private,max-age=10",0}
    // {"private",0},
    // {"private,s-maxage=8",0},
};

class EffectiveMaxAgeTest : public testing::TestWithParam<EffectiveMaxAgeParams> {};

INSTANTIATE_TEST_SUITE_P(EffectiveMaxAgeTest, EffectiveMaxAgeTest, testing::ValuesIn(params));

TEST_P(EffectiveMaxAgeTest, EffectiveMaxAgeTest) {
  EXPECT_EQ(HttpCacheUtils::effectiveMaxAge(GetParam().cache_control),
            std::chrono::seconds(GetParam().effective_max_age_secs));
}

void testReadAndRemoveLeadingDigits(absl::string_view s, int64_t expected,
                                    absl::string_view remaining) {
  absl::string_view input(s);
  auto output = HttpCacheUtils::readAndRemoveLeadingDigits(input);
  if (output) {
    EXPECT_EQ(output, static_cast<uint64_t>(expected));
    EXPECT_EQ(input, remaining);
  } else {
    EXPECT_LT(expected, 0);
    EXPECT_EQ(input, remaining);
  }
}

TEST(StringUtil, ReadAndRemoveLeadingDigits) {
  testReadAndRemoveLeadingDigits("123", 123, "");
  testReadAndRemoveLeadingDigits("a123", -1, "a123");
  testReadAndRemoveLeadingDigits("9_", 9, "_");
  testReadAndRemoveLeadingDigits("11111111111xyz", 11111111111ll, "xyz");

  // Overflow case
  testReadAndRemoveLeadingDigits("1111111111111111111111111111111xyz", -1,
                                 "1111111111111111111111111111111xyz");

  // 2^64
  testReadAndRemoveLeadingDigits("18446744073709551616xyz", -1, "18446744073709551616xyz");
  // 2^64-1
  testReadAndRemoveLeadingDigits("18446744073709551615xyz", 18446744073709551615ull, "xyz");
  // (2^64-1)*10+9
  testReadAndRemoveLeadingDigits("184467440737095516159yz", -1, "184467440737095516159yz");
}

TEST(StringUtil, ReadAndRemoveLeadingDigitsExhaustive) {
  // Pseudo-exhaustive test.
  // We run through every possible 16-20 digit number, where the middle
  // 14 digits consist of the same 2 digits repeated 7 times, as well as
  // every 16-digit number where the middle 14 digits are 67440737095516,
  // which is the middle digits of 2^64. And we run them twice, once as
  // just the number, and again with a trailing "X".
  // The "same 2 digits" are all combos 00-99 for low and high values of
  // leading 4 digits, and all combos divisible by 3 when the first 4
  // numbers are 1000-8999.
  bool saw_264 = false, saw_264_minus_1 = false;
  for (int hi4 = 0; hi4 <= 9999; ++hi4) {
    for (int mid2 = 0; mid2 <= 100; ++mid2) {
      char buf[32];
      if (mid2 < 100) {
        snprintf(buf, sizeof(buf), "%d%02d%02d%02d%02d%02d%02d%02d", hi4, mid2, mid2, mid2, mid2,
                 mid2, mid2, mid2);
      } else {
        snprintf(buf, sizeof(buf), "%d67440737095516", hi4);
      }
      uint64_t expected = hi4 * 100000000000000 + mid2 * 1010101010101;
      if (mid2 == 100) {
        expected = hi4 * 100000000000000 + 67440737095516;
      }

      absl::string_view input(buf);
      absl::optional<uint64_t> output = HttpCacheUtils::readAndRemoveLeadingDigits(input);
      EXPECT_TRUE(output) << " given " << buf;
      EXPECT_EQ(expected, output);
      EXPECT_EQ(0, input.size());

      // To save time, use the leading digits over and over again, hand-placing
      // the final digits at the end of buf.
      char* write = &buf[strlen(buf)];
      int lo2_inc = 1;
      if (hi4 > 999 && hi4 < 9000 && hi4 != 1844) {
        // To save time, only do 1/3rd of the possibilities for the last
        // digits, when the first four digits are between 1000 and 9000.
        // This doubles the speed while still still doing a lot of testing.
        lo2_inc += 2;
      }
      for (int lo2 = 0; lo2 <= 99; lo2 += lo2_inc) {
        write[0] = '0' + lo2 / 10;
        write[1] = '0' + lo2 % 10;
        write[2] = '\0';
        absl::string_view big_input(buf);
        uint64_t big_expected = expected * 100 + lo2;
        if (big_input == "18446744073709551616") {
          saw_264 = true;
        }
        if (big_input == "18446744073709551615") {
          saw_264_minus_1 = true;
        }
        absl::optional<uint64_t> big_output;
        if (big_expected / 100 != expected) { // overflow
          big_output = HttpCacheUtils::readAndRemoveLeadingDigits(big_input);
          EXPECT_FALSE(big_output) << " given overflowing " << buf;
        } else {
          big_output = HttpCacheUtils::readAndRemoveLeadingDigits(big_input);
          EXPECT_TRUE(big_output) << " given " << buf;
          EXPECT_EQ(big_expected, big_output) << " given " << buf;
          EXPECT_EQ(0, big_input.size());
          write[2] = 'X';
          write[3] = '\0';
          big_input = buf;
          big_output = HttpCacheUtils::readAndRemoveLeadingDigits(big_input);
          EXPECT_TRUE(big_output) << " given " << buf;
          EXPECT_EQ(big_expected, big_output) << " given " << buf;
          EXPECT_EQ(1, big_input.size());
        }
      }
    }
  }
  EXPECT_TRUE(saw_264);
  EXPECT_TRUE(saw_264_minus_1);
}
} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
