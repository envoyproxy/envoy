#include "source/extensions/filters/http/cdn_loop/utils.h"

#include "test/test_common/status_utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {
namespace {

using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::Envoy::StatusHelpers::StatusIs;

TEST(CountCdnLoopOccurrencesTest, EmptyHeader) {
  EXPECT_THAT(countCdnLoopOccurrences("", "cdn"), IsOkAndHolds(0));
}

TEST(CountCdnLoopOccurrencesTest, NoParameterTests) {
  // A pseudonym
  EXPECT_THAT(countCdnLoopOccurrences("cdn", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, CDN", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, CDN", "CDN"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, CDN", "foo"), IsOkAndHolds(0));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, cdn, cdn", "cdn"), IsOkAndHolds(3));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, cdn, cdn", "foo"), IsOkAndHolds(0));

  // A DNS name
  EXPECT_THAT(countCdnLoopOccurrences("cdn.example.com", "cdn.example.com"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn.example.com, CDN", "cdn.example.com"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn.example.com, CDN", "CDN"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn.example.com, CDN", "foo"), IsOkAndHolds(0));
  EXPECT_THAT(countCdnLoopOccurrences("cdn.example.com, cdn.example.com, cdn.example.com",
                                      "cdn.example.com"),
              IsOkAndHolds(3));
  EXPECT_THAT(countCdnLoopOccurrences("cdn.example.com, cdn.example.com, cdn.example.com", "foo"),
              IsOkAndHolds(0));

  // IPv4 Addresses
  EXPECT_THAT(countCdnLoopOccurrences("192.0.2.1", "192.0.2.1"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("192.0.2.1, CDN", "192.0.2.1"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("192.0.2.1, CDN", "CDN"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("192.0.2.1, CDN", "foo"), IsOkAndHolds(0));
  EXPECT_THAT(countCdnLoopOccurrences("192.0.2.1, 192.0.2.1, 192.0.2.1", "192.0.2.1"),
              IsOkAndHolds(3));
  EXPECT_THAT(countCdnLoopOccurrences("192.0.2.1, 192.0.2.1, 192.0.2.1", "foo"), IsOkAndHolds(0));

  // IpV6 Addresses
  EXPECT_THAT(countCdnLoopOccurrences("[2001:DB8::3]", "[2001:DB8::3]"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("[2001:DB8::3], CDN", "[2001:DB8::3]"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("[2001:DB8::3], CDN", "CDN"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("[2001:DB8::3], CDN", "foo"), IsOkAndHolds(0));
  EXPECT_THAT(
      countCdnLoopOccurrences("[2001:DB8::3], [2001:DB8::3], [2001:DB8::3]", "[2001:DB8::3]"),
      IsOkAndHolds(3));
  EXPECT_THAT(countCdnLoopOccurrences("[2001:DB8::3], [2001:DB8::3], [2001:DB8::3]", "foo"),
              IsOkAndHolds(0));
}

TEST(CountCdnLoopOccurrencesTest, SimpleParameterTests) {
  EXPECT_THAT(countCdnLoopOccurrences("cdn; foo=bar", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn; foo=bar, CDN", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn; foo=bar; baz=quux, CDN", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, cdn; foo=bar, cdn; baz=quux", "cdn"), IsOkAndHolds(3));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, cdn; foo=bar; baz=quux, cdn", "foo"), IsOkAndHolds(0));
}

TEST(CountCdnLoopOccurrencesTest, ExcessWhitespace) {
  EXPECT_THAT(countCdnLoopOccurrences("  cdn", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn  ", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences(" cdn ", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("\tcdn\t", "cdn"), IsOkAndHolds(1));
}

TEST(CountCdnLoopOccurrencesTest, NoWhitespace) {
  EXPECT_THAT(countCdnLoopOccurrences("cdn", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn,cdn", "cdn"), IsOkAndHolds(2));
  EXPECT_THAT(countCdnLoopOccurrences("cdn;foo=bar;baz=quuz,cdn", "cdn"), IsOkAndHolds(2));
}

TEST(CountCdnLoopOccurrencesTest, CdnIdInParameterTests) {
  // In these tests, the parameter contains a string matching the cdn_id in
  // either the key or the value of the parameters.
  EXPECT_THAT(countCdnLoopOccurrences("cdn; cdn=bar", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn; foo=cdn", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn; cdn=cdn", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn; cdn=\"cdn\"", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn; cdn=\"cdn,cdn\"", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn; cdn=\"cdn, cdn\"", "cdn"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences("cdn, cdn; cdn=\"cdn\", cdn ; cdn=\"cdn,cdn\"", "cdn"),
              IsOkAndHolds(3));
}

TEST(CountCdnLoopOccurrencesTest, Rfc8586Tests) {
  // Examples from RFC 8586, Section 2.
  const std::string example1 = "foo123.foocdn.example, barcdn.example; trace=\"abcdef\"";
  EXPECT_THAT(countCdnLoopOccurrences(example1, "foo123.foocdn.example"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences(example1, "barcdn.example"), IsOkAndHolds(1));
  EXPECT_THAT(countCdnLoopOccurrences(example1, "trace=\"abcdef\""), IsOkAndHolds(0));
  const std::string example2 = "AnotherCDN; abc=123; def=\"456\"";
  EXPECT_THAT(countCdnLoopOccurrences(example2, "AnotherCDN"), IsOkAndHolds(1));

  // The concatenation of the two done correctly as per RFC 7230 rules
  {
    const std::string combined = absl::StrCat(example1, ",", example2);
    EXPECT_THAT(countCdnLoopOccurrences(combined, "foo123.foocdn.example"), IsOkAndHolds(1));
    EXPECT_THAT(countCdnLoopOccurrences(combined, "barcdn.example"), IsOkAndHolds(1));
    EXPECT_THAT(countCdnLoopOccurrences(combined, "AnotherCDN"), IsOkAndHolds(1));
  }

  // The concatenation of two done poorly (with extra commas)
  {
    const std::string combined = absl::StrCat(example1, ",,,", example2);
    EXPECT_THAT(countCdnLoopOccurrences(combined, "foo123.foocdn.example"), IsOkAndHolds(1));
    EXPECT_THAT(countCdnLoopOccurrences(combined, "barcdn.example"), IsOkAndHolds(1));
    EXPECT_THAT(countCdnLoopOccurrences(combined, "AnotherCDN"), IsOkAndHolds(1));
  }
}

TEST(CountCdnLoopOccurrencesTest, ValidHeaderInsideParameter) {
  EXPECT_THAT(countCdnLoopOccurrences("cdn; header=\"cdn; cdn=cdn; cdn\"", "cdn"), IsOkAndHolds(1));
}

TEST(CountCdnLoopOccurrencesTest, BadCdnId) {
  EXPECT_THAT(countCdnLoopOccurrences("cdn", ""), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CountCdnLoopOccurrencesTest, BadHeader) {
  EXPECT_THAT(countCdnLoopOccurrences("[bad-id", "cdn"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

} // namespace
} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
