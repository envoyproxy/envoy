#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "source/extensions/filters/http/cache/range_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

TEST(RawByteRangeTest, IsSuffix) {
  auto r = RawByteRange(UINT64_MAX, 4);
  ASSERT_TRUE(r.isSuffix());
}

TEST(RawByteRangeTest, IsNotSuffix) {
  auto r = RawByteRange(3, 4);
  ASSERT_FALSE(r.isSuffix());
}

TEST(RawByteRangeTest, FirstBytePos) {
  auto r = RawByteRange(3, 4);
  ASSERT_EQ(3, r.firstBytePos());
}

TEST(RawByteRangeTest, LastBytePos) {
  auto r = RawByteRange(3, 4);
  ASSERT_EQ(4, r.lastBytePos());
}

TEST(RawByteRangeTest, SuffixLength) {
  auto r = RawByteRange(UINT64_MAX, 4);
  ASSERT_EQ(4, r.suffixLength());
}

TEST(AdjustedByteRangeTest, Length) {
  auto a = AdjustedByteRange(3, 6);
  ASSERT_EQ(3, a.length());
}

TEST(AdjustedByteRangeTest, TrimFront) {
  auto a = AdjustedByteRange(3, 6);
  a.trimFront(2);
  ASSERT_EQ(5, a.begin());
}

TEST(AdjustedByteRangeTest, MaxLength) {
  auto a = AdjustedByteRange(0, UINT64_MAX);
  ASSERT_EQ(UINT64_MAX, a.length());
}

TEST(AdjustedByteRangeTest, MaxTrim) {
  auto a = AdjustedByteRange(0, UINT64_MAX);
  a.trimFront(UINT64_MAX);
  ASSERT_EQ(0, a.length());
}

struct AdjustByteRangeParams {
  std::vector<RawByteRange> request;
  std::vector<AdjustedByteRange> result;
  uint64_t content_length;
};

AdjustByteRangeParams satisfiable_ranges[] =
    // request, result, content_length
    {
        // Various ways to request the full body. Full responses are signaled by
        // empty result vectors.
        {{{0, 3}}, {}, 4},                       // byte-range-spec, exact
        {{{UINT64_MAX, 4}}, {}, 4},              // suffix-byte-range-spec, exact
        {{{0, 99}}, {}, 4},                      // byte-range-spec, overlong
        {{{0, UINT64_MAX}}, {}, 4},              // byte-range-spec, overlong
        {{{UINT64_MAX, 5}}, {}, 4},              // suffix-byte-range-spec, overlong
        {{{UINT64_MAX, UINT64_MAX - 1}}, {}, 4}, // suffix-byte-range-spec, overlong
        {{{UINT64_MAX, UINT64_MAX}}, {}, 4},     // suffix-byte-range-spec, overlong

        // Single bytes
        {{{0, 0}}, {{0, 1}}, 4},
        {{{1, 1}}, {{1, 2}}, 4},
        {{{3, 3}}, {{3, 4}}, 4},
        {{{UINT64_MAX, 1}}, {{3, 4}}, 4},

        // Multiple bytes, starting in the middle
        {{{1, 2}}, {{1, 3}}, 4},           // fully in the middle
        {{{1, 3}}, {{1, 4}}, 4},           // to the end
        {{{2, 21}}, {{2, 4}}, 4},          // overlong
        {{{1, UINT64_MAX}}, {{1, 4}}, 4}}; // overlong
// TODO(toddmgreer): Before enabling support for multi-range requests, test it.

class CreateAdjustedRangeDetailsTest : public testing::TestWithParam<AdjustByteRangeParams> {};

TEST_P(CreateAdjustedRangeDetailsTest, All) {
  RangeDetails result =
      RangeUtils::createAdjustedRangeDetails(GetParam().request, GetParam().content_length);
  ASSERT_TRUE(result.satisfiable_);
  EXPECT_THAT(result.ranges_, testing::ContainerEq(GetParam().result));
}

INSTANTIATE_TEST_SUITE_P(CreateAdjustedRangeDetailsTest, CreateAdjustedRangeDetailsTest,
                         testing::ValuesIn(satisfiable_ranges));

class AdjustByteRangeUnsatisfiableTest : public testing::TestWithParam<std::vector<RawByteRange>> {
};

std::vector<RawByteRange> unsatisfiable_ranges[] = {
    {{4, 5}},
    {{4, 9}},
    {{7, UINT64_MAX}},
    {{UINT64_MAX, 0}},
};

TEST_P(AdjustByteRangeUnsatisfiableTest, All) {
  RangeDetails result = RangeUtils::createAdjustedRangeDetails(GetParam(), 3);
  ASSERT_FALSE(result.satisfiable_);
}

INSTANTIATE_TEST_SUITE_P(AdjustByteRangeUnsatisfiableTest, AdjustByteRangeUnsatisfiableTest,
                         testing::ValuesIn(unsatisfiable_ranges));

TEST(AdjustByteRange, NoRangeRequest) {
  RangeDetails result = RangeUtils::createAdjustedRangeDetails({}, 8);
  ASSERT_TRUE(result.satisfiable_);
  EXPECT_THAT(result.ranges_, testing::ContainerEq(std::vector<AdjustedByteRange>{}));
}

TEST(ParseRangeHeaderTest, InvalidUnit) {
  absl::optional<std::vector<RawByteRange>> result = RangeUtils::parseRangeHeader("bits=3-4", 5);

  ASSERT_FALSE(result.has_value());
}

TEST(ParseRangeHeaderTest, SingleRange) {
  absl::optional<std::vector<RawByteRange>> result = RangeUtils::parseRangeHeader("bytes=3-4", 5);

  ASSERT_TRUE(result.has_value());
  auto result_vector = result.value();

  ASSERT_EQ(1, result_vector.size());

  EXPECT_EQ(3, result_vector[0].firstBytePos());
  EXPECT_EQ(4, result_vector[0].lastBytePos());
}

TEST(ParseRangeHeaderTest, MissingFirstBytePos) {
  absl::optional<std::vector<RawByteRange>> result = RangeUtils::parseRangeHeader("bytes=-5", 5);

  ASSERT_TRUE(result.has_value());
  auto result_vector = result.value();
  ASSERT_EQ(1, result_vector.size());

  EXPECT_TRUE(result_vector[0].isSuffix());
  EXPECT_EQ(5, result_vector[0].suffixLength());
}

TEST(ParseRangeHeaderTest, MissingLastBytePos) {
  absl::optional<std::vector<RawByteRange>> result = RangeUtils::parseRangeHeader("bytes=6-", 5);

  ASSERT_TRUE(result.has_value());
  auto result_vector = result.value();

  ASSERT_EQ(1, result_vector.size());

  EXPECT_EQ(6, result_vector[0].firstBytePos());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(), result_vector[0].lastBytePos());
}

TEST(ParseRangeHeaderTest, MultipleRanges) {
  absl::optional<std::vector<RawByteRange>> result =
      RangeUtils::parseRangeHeader("bytes=345-456,-567,6789-", 5);

  ASSERT_TRUE(result.has_value());
  auto result_vector = result.value();

  ASSERT_EQ(3, result_vector.size());

  EXPECT_EQ(345, result_vector[0].firstBytePos());
  EXPECT_EQ(456, result_vector[0].lastBytePos());

  EXPECT_TRUE(result_vector[1].isSuffix());
  EXPECT_EQ(567, result_vector[1].suffixLength());

  EXPECT_EQ(6789, result_vector[2].firstBytePos());
  EXPECT_EQ(UINT64_MAX, result_vector[2].lastBytePos());
}

TEST(ParseRangeHeaderTest, LongRangeHeaderValue) {
  absl::string_view header_value = "bytes=1000-1000,1001-1001,1002-1002,1003-1003,1004-1004,1005-"
                                   "1005,1006-1006,1007-1007,1008-1008,100-";
  absl::optional<std::vector<RawByteRange>> result = RangeUtils::parseRangeHeader(header_value, 10);

  ASSERT_TRUE(result.has_value());
  auto result_vector = result.value();

  ASSERT_EQ(10, result_vector.size());
}

TEST(ParseRangeHeaderTest, ZeroRangeLimit) {
  absl::optional<std::vector<RawByteRange>> result =
      RangeUtils::parseRangeHeader("bytes=1000-1000", 0);

  ASSERT_FALSE(result.has_value());
}

TEST(ParseRangeHeaderTest, OverRangeLimit) {
  absl::optional<std::vector<RawByteRange>> result =
      RangeUtils::parseRangeHeader("bytes=1000-1000,1001-1001", 1);

  ASSERT_FALSE(result.has_value());
}

class ParseInvalidRangeHeaderTest : public testing::Test,
                                    public testing::WithParamInterface<absl::string_view> {
protected:
  absl::string_view headerValue() { return GetParam(); }
};

// clang-format off
INSTANTIATE_TEST_SUITE_P(
    Default, ParseInvalidRangeHeaderTest,
    testing::Values("-",
                    "1-2",
                    "12",
                    "a",
                    "a1",
                    "bytes=",
                    "bytes=-",
                    "bytes1-2",
                    "bytes=12",
                    "bytes=1-2-3",
                    "bytes=1-2-",
                    "bytes=1--3",
                    "bytes=--2",
                    "bytes=2--",
                    "bytes=-2-",
                    "bytes=-1-2",
                    "bytes=a-2",
                    "bytes=2-a",
                    "bytes=-a",
                    "bytes=a-",
                    "bytes=a1-2",
                    "bytes=1-a2",
                    "bytes=1a-2",
                    "bytes=1-2a",
                    "bytes=1-2,3-a",
                    "bytes=1-a,3-4",
                    "bytes=1-2,3a-4",
                    "bytes=1-2,3-4a",
                    "bytes=1-2,3-4-5",
                    "bytes=1-2,bytes=3-4",
                    "bytes=1-2,3-4,a",
                    // too many byte ranges (test sets the limit as 5)
                    "bytes=0-1,1-2,2-3,3-4,4-5,5-6",
                    // UINT64_MAX-UINT64_MAX+1
                    "bytes=18446744073709551615-18446744073709551616",
                    // UINT64_MAX+1-UINT64_MAX+2
                    "bytes=18446744073709551616-18446744073709551617"));
// clang-format on

TEST_P(ParseInvalidRangeHeaderTest, InvalidRangeReturnsEmpty) {
  absl::optional<std::vector<RawByteRange>> result = RangeUtils::parseRangeHeader(headerValue(), 5);
  ASSERT_FALSE(result.has_value());
}

TEST(CreateRangeDetailsTest, NoRangeHeader) {
  Envoy::Http::TestRequestHeaderMapImpl headers =
      Envoy::Http::TestRequestHeaderMapImpl{{":method", "GET"}};
  absl::optional<RangeDetails> result = RangeUtils::createRangeDetails(headers, 5);

  ASSERT_FALSE(result.has_value());
}

TEST(CreateRangeDetailsTest, SingleSatisfiableRange) {
  Envoy::Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                        {":method", "GET"},
                                                        {"x-forwarded-proto", "https"},
                                                        {":authority", "example.com"},
                                                        {"range", "bytes=1-99"}};
  const Envoy::Http::HeaderMap::GetResult range_header =
      request_headers.get(Envoy::Http::Headers::get().Range);
  absl::optional<RangeDetails> result = RangeUtils::createRangeDetails(request_headers, 4);
  ASSERT_TRUE(result.has_value());
  RangeDetails& spec = result.value();
  EXPECT_TRUE(spec.satisfiable_);
  ASSERT_EQ(spec.ranges_.size(), 1);

  AdjustedByteRange& range = spec.ranges_[0];
  EXPECT_EQ(range.begin(), 1);
  EXPECT_EQ(range.end(), 4);
  EXPECT_EQ(range.length(), 3);
}

TEST(GetRangeDetailsTest, MultipleSatisfiableRanges) {
  // Because we do not support multi-part responses for now, we are limiting
  // parsing of a single range, so we return false to indicate to the
  // CacheFilter that the request should be handled as if this were not a range
  // request.

  Envoy::Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                        {":method", "GET"},
                                                        {"x-forwarded-proto", "https"},
                                                        {":authority", "example.com"},
                                                        {"range", "bytes=1-99,3-,-3"}};

  absl::optional<RangeDetails> result = RangeUtils::createRangeDetails(request_headers, 4);

  EXPECT_FALSE(result.has_value());
}

TEST(GetRangeDetailsTest, NotSatisfiableRange) {
  Envoy::Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                        {":method", "GET"},
                                                        {"x-forwarded-proto", "https"},
                                                        {":authority", "example.com"},
                                                        {"range", "bytes=100-"}};

  absl::optional<RangeDetails> result = RangeUtils::createRangeDetails(request_headers, 4);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->satisfiable_);
  ASSERT_TRUE(result->ranges_.empty());
}

// operator<<(ostream&, const AdjustedByteRange&) is only used in tests, but lives in //source,
// and so needs test coverage. This test provides that coverage, to keep the coverage test happy.
TEST(AdjustedByteRange, StreamingTest) {
  std::ostringstream os;
  os << AdjustedByteRange(0, 1);
  EXPECT_EQ(os.str(), "[0,1)");
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
