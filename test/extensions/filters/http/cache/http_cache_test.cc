#include "extensions/filters/http/cache/cache_headers_utils.h"
#include "extensions/filters/http/cache/http_cache.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::TestWithParam;
using testing::ValuesIn;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
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

class LookupRequestTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_source_;
  SystemTime current_time_ = time_source_.systemTime();
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {"x-forwarded-proto", "https"}, {":authority", "example.com"}};
};

LookupResult makeLookupResult(const LookupRequest& lookup_request,
                              const Http::TestResponseHeaderMapImpl& response_headers,
                              uint64_t content_length = 0) {
  return lookup_request.makeLookupResult(
      std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers), content_length);
}

TEST_F(LookupRequestTest, MakeLookupResultNoBody) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, MakeLookupResultBody) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const uint64_t content_length = 5;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, content_length);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, PrivateResponse) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"age", "2"},
       {"cache-control", "private, max-age=3600"},
       {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  // We must make sure at cache insertion time, private responses must not be
  // inserted. However, if the insertion did happen, it would be served at the
  // time of lookup. (Nothing should rely on this.)
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, Expired) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", "Thu, 01 Jan 2019 00:00:00 GMT"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, ExpiredViaFallbackheader) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"expires", formatter_.fromTime(current_time_ - std::chrono::seconds(5))},
       {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, NotExpiredViaFallbackheader) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"expires", formatter_.fromTime(current_time_ + std::chrono::seconds(5))},
       {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, FullRange) {
  request_headers_.addCopy("Range", "0-99");
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)},
       {"cache-control", "public, max-age=3600"},
       {"content-length", "4"}});
  const uint64_t content_length = 4;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);
  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 4);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

struct AdjustByteRangeParams {
  std::vector<RawByteRange> request;
  std::vector<AdjustedByteRange> result;
  uint64_t content_length;
};

AdjustByteRangeParams satisfiable_ranges[] =
    // request, result, content_length
    {
        // Various ways to request the full body. Full responses are signaled by empty result
        // vectors.
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

class AdjustByteRangeTest : public TestWithParam<AdjustByteRangeParams> {};

TEST_P(AdjustByteRangeTest, All) {
  std::vector<AdjustedByteRange> result;
  ASSERT_TRUE(adjustByteRangeSet(result, GetParam().request, GetParam().content_length));
  EXPECT_THAT(result, ContainerEq(GetParam().result));
}

INSTANTIATE_TEST_SUITE_P(AdjustByteRangeTest, AdjustByteRangeTest, ValuesIn(satisfiable_ranges));

class AdjustByteRangeUnsatisfiableTest : public TestWithParam<std::vector<RawByteRange>> {};

std::vector<RawByteRange> unsatisfiable_ranges[] = {
    {{4, 5}},
    {{4, 9}},
    {{7, UINT64_MAX}},
    {{UINT64_MAX, 0}},
};

TEST_P(AdjustByteRangeUnsatisfiableTest, All) {
  std::vector<AdjustedByteRange> result;
  ASSERT_FALSE(adjustByteRangeSet(result, GetParam(), 3));
}

INSTANTIATE_TEST_SUITE_P(AdjustByteRangeUnsatisfiableTest, AdjustByteRangeUnsatisfiableTest,
                         ValuesIn(unsatisfiable_ranges));

TEST(AdjustByteRange, NoRangeRequest) {
  std::vector<AdjustedByteRange> result;
  ASSERT_TRUE(adjustByteRangeSet(result, {}, 8));
  EXPECT_THAT(result, ContainerEq(std::vector<AdjustedByteRange>{}));
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
