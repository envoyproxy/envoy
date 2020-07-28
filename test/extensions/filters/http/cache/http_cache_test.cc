#include <chrono>

#include "extensions/filters/http/cache/cache_headers_utils.h"
#include "extensions/filters/http/cache/http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
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

TEST_F(LookupRequestTest, FutureResponseTime) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  const SystemTime response_time = current_time_ + std::chrono::seconds(1);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(response_time)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
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

TEST_F(LookupRequestTest, RequestRequiresValidation) {
  // Make sure the request requires validation (even though the response is fresh)
  request_headers_.addCopy("cache-control", "no-cache");
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, ResponseRequiresValidation) {
  const LookupRequest lookup_request(request_headers_, current_time_);
  // The response requires revalidation on every request
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, no-cache"}, {"date", formatter_.fromTime(current_time_)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMaxAgeSatisfied) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  // The request will not accept responses with age higher than 10 s, even if they are still fresh
  request_headers_.addCopy("cache-control", "max-age=10");
  const SystemTime request_time = current_time_ + std::chrono::seconds(9);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMaxAgeUnsatisfied) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  // The request will not accept responses with age higher than 10 s, even if they are still fresh
  request_headers_.addCopy("cache-control", "max-age=10");
  const SystemTime request_time = current_time_ + std::chrono::seconds(20);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMinFreshSatisfied) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  // The request will not accept responses that have less than 1000 s left to be expired
  request_headers_.addCopy("cache-control", "min-fresh=1000");
  const SystemTime request_time = current_time_ + std::chrono::seconds(2599);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMinFreshUnsatisfied) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  // The request will not accept responses that have less than 1000 s left to be expired
  request_headers_.addCopy("cache-control", "min-fresh=1000");
  const SystemTime request_time = current_time_ + std::chrono::seconds(2601);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMaxAgeSatisfiedMinFreshUnsatisfied) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  // The request will not accept responses with age higher than 3000 s, even if they are still fresh
  // The request will not accept responses that have less than 1000 s left to be expired
  request_headers_.addCopy("cache-control", "max-age=3000, min-fresh=1000");
  const SystemTime request_time = current_time_ + std::chrono::seconds(2601);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, Expired) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  const SystemTime request_time = current_time_ + std::chrono::seconds(3601);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMaxStaleSatisfied) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  // The request will accept a stale response if it has been expired for less than 500 s
  request_headers_.addCopy("cache-control", "max-stale=500");
  const SystemTime request_time = current_time_ + std::chrono::seconds(4099);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMaxStaleUnatisfied) {
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600"}, {"date", formatter_.fromTime(current_time_)}});
  // The request will accept a stale response if it has been expired for less than 500 s
  request_headers_.addCopy("cache-control", "max-stale=500");
  const SystemTime request_time = current_time_ + std::chrono::seconds(4101);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, RequestMaxStaleResponseMustRevalidate) {
  // The response does not allow being served stale
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", "public, max-age=3600, must-revalidate"},
       {"date", formatter_.fromTime(current_time_)}});
  // The request will accept a stale response if it has been expired for less than 500 s
  request_headers_.addCopy("cache-control", "max-stale=500");
  const SystemTime request_time = current_time_ + std::chrono::seconds(4099);
  const LookupRequest lookup_request(request_headers_, request_time);
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  // The response should be revalidated even though the request would accept it
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

// If request Cache-Control header is missing,
// "Pragma:no-cache" is equivalent to "Cache-Control:no-cache".
// https://httpwg.org/specs/rfc7234.html#header.pragma
TEST_F(LookupRequestTest, PragmaNoCacheFallback) {
  request_headers_.addCopy("pragma", "no-cache");
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is not expired but the request requires revalidation through Pragma: no-cache.
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaNoCacheFallbackExtraDirectivesIgnored) {
  request_headers_.addCopy("pragma", "no-cache, custom-directive=custom-value");
  const LookupRequest lookup_request(request_headers_, current_time_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is not expired but the request requires revalidation through Pragma: no-cache.
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaFallbackOtherValuesIgnored) {
  request_headers_.addCopy("pragma", "max-age=0");
  const LookupRequest lookup_request(request_headers_, current_time_ + std::chrono::seconds(5));
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is fresh, Pragma header with values other than "no-cache" is ignored.
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaNoFallback) {
  request_headers_.addCopy("pragma", "no-cache");
  request_headers_.addCopy("cache-control", "max-age=10");
  const LookupRequest lookup_request(request_headers_, current_time_ + std::chrono::seconds(5));
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Pragma header is ignored when Cache-Control header is present.
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, SatisfiableRange) {
  // add method (GET) and range to headers
  request_headers_.addReference(Http::Headers::get().Method, Http::Headers::get().MethodValues.Get);
  request_headers_.addReference(Http::Headers::get().Range, "bytes=1-99,3-,-2");
  const LookupRequest lookup_request(request_headers_, current_time_);

  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)},
       {"cache-control", "public, max-age=3600"},
       {"content-length", "4"}});
  const uint64_t content_length = 4;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);
  ASSERT_EQ(CacheEntryStatus::SatisfiableRange, lookup_response.cache_entry_status_);

  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 4);

  // checks that the ranges have been adjusted to the content's length
  EXPECT_EQ(lookup_response.response_ranges_.size(), 3);

  EXPECT_EQ(lookup_response.response_ranges_[0].begin(), 1);
  EXPECT_EQ(lookup_response.response_ranges_[0].end(), 4);
  EXPECT_EQ(lookup_response.response_ranges_[0].length(), 3);

  EXPECT_EQ(lookup_response.response_ranges_[1].begin(), 3);
  EXPECT_EQ(lookup_response.response_ranges_[1].end(), 4);
  EXPECT_EQ(lookup_response.response_ranges_[1].length(), 1);

  EXPECT_EQ(lookup_response.response_ranges_[2].begin(), 2);
  EXPECT_EQ(lookup_response.response_ranges_[2].end(), 4);
  EXPECT_EQ(lookup_response.response_ranges_[2].length(), 2);

  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, NotSatisfiableRange) {
  // add method (GET) and range headers
  request_headers_.addReference(Http::Headers::get().Method, Http::Headers::get().MethodValues.Get);
  request_headers_.addReference(Http::Headers::get().Range, "bytes=5-99,100-");

  const LookupRequest lookup_request(request_headers_, current_time_);

  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(current_time_)},
       {"cache-control", "public, max-age=3600"},
       {"content-length", "4"}});
  const uint64_t content_length = 4;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);
  ASSERT_EQ(CacheEntryStatus::NotSatisfiableRange, lookup_response.cache_entry_status_);

  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 4);
  ASSERT_TRUE(lookup_response.response_ranges_.empty());
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

namespace {
Http::TestRequestHeaderMapImpl makeTestHeaderMap(std::string range_value) {
  return Http::TestRequestHeaderMapImpl{{":method", "GET"}, {"range", range_value}};
}
} // namespace

TEST(ParseRangesTest, NoRangeHeader) {
  Http::TestRequestHeaderMapImpl headers = Http::TestRequestHeaderMapImpl{{":method", "GET"}};
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 5);

  ASSERT_EQ(0, result_vector.size());
}

TEST(ParseRangesTest, InvalidUnit) {
  Http::TestRequestHeaderMapImpl headers = makeTestHeaderMap("bits=3-4");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 5);

  ASSERT_EQ(0, result_vector.size());
}

TEST(ParseRangesTest, SingleRange) {
  Http::TestRequestHeaderMapImpl headers = makeTestHeaderMap("bytes=3-4");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 5);

  ASSERT_EQ(1, result_vector.size());

  ASSERT_EQ(3, result_vector[0].firstBytePos());
  ASSERT_EQ(4, result_vector[0].lastBytePos());
}

TEST(ParseRangesTest, MissingFirstBytePos) {
  Http::TestRequestHeaderMapImpl headers = makeTestHeaderMap("bytes=-5");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 5);

  ASSERT_EQ(1, result_vector.size());

  ASSERT_TRUE(result_vector[0].isSuffix());
  ASSERT_EQ(5, result_vector[0].suffixLength());
}

TEST(ParseRangesTest, MissingLastBytePos) {
  Http::TestRequestHeaderMapImpl headers = makeTestHeaderMap("bytes=6-");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 5);

  ASSERT_EQ(1, result_vector.size());

  ASSERT_EQ(6, result_vector[0].firstBytePos());
  ASSERT_EQ(std::numeric_limits<uint64_t>::max(), result_vector[0].lastBytePos());
}

TEST(ParseRangesTest, MultipleRanges) {
  Http::TestRequestHeaderMapImpl headers = makeTestHeaderMap("bytes=345-456,-567,6789-");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 5);

  ASSERT_EQ(3, result_vector.size());

  ASSERT_EQ(345, result_vector[0].firstBytePos());
  ASSERT_EQ(456, result_vector[0].lastBytePos());

  ASSERT_TRUE(result_vector[1].isSuffix());
  ASSERT_EQ(567, result_vector[1].suffixLength());

  ASSERT_EQ(6789, result_vector[2].firstBytePos());
  ASSERT_EQ(UINT64_MAX, result_vector[2].lastBytePos());
}

TEST(ParseRangesTest, LongRangeHeaderValue) {
  Http::TestRequestHeaderMapImpl headers =
      makeTestHeaderMap("bytes=1000-1000,1001-1001,1002-1002,1003-1003,1004-1004,1005-"
                        "1005,1006-1006,1007-1007,1008-1008,100-");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 10);

  ASSERT_EQ(10, result_vector.size());
}

TEST(ParseRangesTest, ZeroRangeLimit) {
  Http::TestRequestHeaderMapImpl headers = makeTestHeaderMap("bytes=1000-1000");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 0);

  ASSERT_EQ(0, result_vector.size());
}

TEST(ParseRangesTest, OverRangeLimit) {
  Http::TestRequestHeaderMapImpl headers = makeTestHeaderMap("bytes=1000-1000,1001-1001");
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(headers, 1);

  ASSERT_EQ(0, result_vector.size());
}

class ParseInvalidRangeHeaderTest : public testing::Test,
                                    public testing::WithParamInterface<std::string> {
protected:
  Http::TestRequestHeaderMapImpl range() { return makeTestHeaderMap(GetParam()); }
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
  std::vector<RawByteRange> result_vector = RangeRequests::parseRanges(range(), 5);
  ASSERT_EQ(0, result_vector.size());
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
