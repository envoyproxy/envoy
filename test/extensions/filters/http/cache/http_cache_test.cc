#include <chrono>
#include <string>

#include "extensions/filters/http/cache/cache_headers_utils.h"
#include "extensions/filters/http/cache/http_cache.h"
#include "extensions/filters/http/cache/inline_headers_handles.h"

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
namespace {

struct LookupRequestTestCase {
  std::string test_name, request_cache_control, response_cache_control;
  SystemTime request_time, response_date;
  CacheEntryStatus expected_cache_entry_status;
  std::string expected_age;
};

using Seconds = std::chrono::seconds;

envoy::extensions::filters::http::cache::v3alpha::CacheConfig getConfig() {
  // Allows 'accept' to be varied in the tests.
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  const auto& add_accept = config.mutable_allowed_vary_headers()->Add();
  add_accept->set_exact("accept");
  return config;
}

class LookupRequestTest : public testing::TestWithParam<LookupRequestTestCase> {
public:
  LookupRequestTest() : vary_allow_list_(getConfig().allowed_vary_headers()) {}

  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestRequestHeaderMapImpl request_headers_{{":path", "/"},
                                                  {":method", "GET"},
                                                  {"x-forwarded-proto", "https"},
                                                  {":authority", "example.com"}};

  VaryHeader vary_allow_list_;

  static const SystemTime& currentTime() {
    CONSTRUCT_ON_FIRST_USE(SystemTime, Event::SimulatedTimeSystem().systemTime());
  }

  static const std::vector<LookupRequestTestCase>& getTestCases() {
    CONSTRUCT_ON_FIRST_USE(std::vector<LookupRequestTestCase>,
                           {"request_requires_revalidation",
                            /*request_cache_control=*/"no-cache",
                            /*response_cache_control=*/"public, max-age=3600",
                            /*request_time=*/currentTime(),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"0"},
                           {"response_requires_revalidation",
                            /*request_cache_control=*/"",
                            /*response_cache_control=*/"no-cache",
                            /*request_time=*/currentTime(),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"0"},
                           {"request_max_age_satisfied",
                            /*request_cache_control=*/"max-age=10",
                            /*response_cache_control=*/"public, max-age=3600",
                            /*request_time=*/currentTime() + Seconds(9),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::Ok,
                            /*expected_age=*/"9"},
                           {"request_max_age_unsatisfied",
                            /*request_cache_control=*/"max-age=10",
                            /*response_cache_control=*/"public, max-age=3600",
                            /*request_time=*/currentTime() + Seconds(11),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"11"},
                           {"request_min_fresh_satisfied",
                            /*request_cache_control=*/"min-fresh=1000",
                            /*response_cache_control=*/"public, max-age=2000",
                            /*request_time=*/currentTime() + Seconds(999),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::Ok,
                            /*expected_age=*/"999"},
                           {"request_min_fresh_unsatisfied",
                            /*request_cache_control=*/"min-fresh=1000",
                            /*response_cache_control=*/"public, max-age=2000",
                            /*request_time=*/currentTime() + Seconds(1001),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1001"},
                           {"request_max_age_satisfied_but_min_fresh_unsatisfied",
                            /*request_cache_control=*/"max-age=1500, min-fresh=1000",
                            /*response_cache_control=*/"public, max-age=2000",
                            /*request_time=*/currentTime() + Seconds(1001),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1001"},
                           {"request_max_age_satisfied_but_max_stale_unsatisfied",
                            /*request_cache_control=*/"max-age=1500, max-stale=400",
                            /*response_cache_control=*/"public, max-age=1000",
                            /*request_time=*/currentTime() + Seconds(1401),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1401"},
                           {"request_max_stale_satisfied_but_min_fresh_unsatisfied",
                            /*request_cache_control=*/"min-fresh=1000, max-stale=500",
                            /*response_cache_control=*/"public, max-age=2000",
                            /*request_time=*/currentTime() + Seconds(1001),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1001"},
                           {"request_max_stale_satisfied_but_max_age_unsatisfied",
                            /*request_cache_control=*/"max-age=1200, max-stale=500",
                            /*response_cache_control=*/"public, max-age=1000",
                            /*request_time=*/currentTime() + Seconds(1201),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1201"},
                           {"request_min_fresh_satisfied_but_max_age_unsatisfied",
                            /*request_cache_control=*/"max-age=500, min-fresh=400",
                            /*response_cache_control=*/"public, max-age=1000",
                            /*request_time=*/currentTime() + Seconds(501),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"501"},
                           {"expired",
                            /*request_cache_control=*/"",
                            /*response_cache_control=*/"public, max-age=1000",
                            /*request_time=*/currentTime() + Seconds(1001),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1001"},
                           {"expired_but_max_stale_satisfied",
                            /*request_cache_control=*/"max-stale=500",
                            /*response_cache_control=*/"public, max-age=1000",
                            /*request_time=*/currentTime() + Seconds(1499),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::Ok,
                            /*expected_age=*/"1499"},
                           {"expired_max_stale_unsatisfied",
                            /*request_cache_control=*/"max-stale=500",
                            /*response_cache_control=*/"public, max-age=1000",
                            /*request_time=*/currentTime() + Seconds(1501),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1501"},
                           {"expired_max_stale_satisfied_but_response_must_revalidate",
                            /*request_cache_control=*/"max-stale=500",
                            /*response_cache_control=*/"public, max-age=1000, must-revalidate",
                            /*request_time=*/currentTime() + Seconds(1499),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::RequiresValidation,
                            /*expected_age=*/"1499"},
                           {"fresh_and_response_must_revalidate",
                            /*request_cache_control=*/"",
                            /*response_cache_control=*/"public, max-age=1000, must-revalidate",
                            /*request_time=*/currentTime() + Seconds(999),
                            /*response_date=*/currentTime(),
                            /*expected_result=*/CacheEntryStatus::Ok,
                            /*expected_age=*/"999"},

    );
  }
};

LookupResult makeLookupResult(const LookupRequest& lookup_request,
                              const Http::TestResponseHeaderMapImpl& response_headers,
                              uint64_t content_length = 0) {
  // For the purpose of the test, set the response_time to the date header value.
  ResponseMetadata metadata = {CacheHeadersUtils::httpTime(response_headers.Date())};
  return lookup_request.makeLookupResult(
      std::make_unique<Http::TestResponseHeaderMapImpl>(response_headers), std::move(metadata),
      content_length);
}

INSTANTIATE_TEST_SUITE_P(ResultMatchesExpectation, LookupRequestTest,
                         testing::ValuesIn(LookupRequestTest::getTestCases()),
                         [](const auto& info) { return info.param.test_name; });

TEST_P(LookupRequestTest, ResultWithoutBodyMatchesExpectation) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                   GetParam().request_cache_control);
  const SystemTime request_time = GetParam().request_time, response_date = GetParam().response_date;
  const LookupRequest lookup_request(request_headers_, request_time, vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", GetParam().response_cache_control},
       {"date", formatter_.fromTime(response_date)}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(GetParam().expected_cache_entry_status, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_THAT(*lookup_response.headers_,
              HeaderHasValueRef(Http::Headers::get().Age, GetParam().expected_age));
  EXPECT_EQ(lookup_response.content_length_, 0);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_P(LookupRequestTest, ResultWithBodyMatchesExpectation) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl,
                                   GetParam().request_cache_control);
  const SystemTime request_time = GetParam().request_time, response_date = GetParam().response_date;
  const LookupRequest lookup_request(request_headers_, request_time, vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"cache-control", GetParam().response_cache_control},
       {"date", formatter_.fromTime(response_date)}});
  const uint64_t content_length = 5;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);

  EXPECT_EQ(GetParam().expected_cache_entry_status, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_THAT(*lookup_response.headers_,
              HeaderHasValueRef(Http::Headers::get().Age, GetParam().expected_age));
  EXPECT_EQ(lookup_response.content_length_, content_length);
  EXPECT_TRUE(lookup_response.response_ranges_.empty());
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, ExpiredViaFallbackheader) {
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"expires", formatter_.fromTime(currentTime() - Seconds(5))},
       {"date", formatter_.fromTime(currentTime())}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);

  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, NotExpiredViaFallbackheader) {
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"expires", formatter_.fromTime(currentTime() + Seconds(5))},
       {"date", formatter_.fromTime(currentTime())}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

// If request Cache-Control header is missing,
// "Pragma:no-cache" is equivalent to "Cache-Control:no-cache".
// https://httpwg.org/specs/rfc7234.html#header.pragma
TEST_F(LookupRequestTest, PragmaNoCacheFallback) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Pragma, "no-cache");
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is not expired but the request requires revalidation through Pragma: no-cache.
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaNoCacheFallbackExtraDirectivesIgnored) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Pragma,
                                   "no-cache, custom-directive=custom-value");
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is not expired but the request requires revalidation through Pragma: no-cache.
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaFallbackOtherValuesIgnored) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Pragma, "max-age=0");
  const LookupRequest lookup_request(request_headers_, currentTime() + Seconds(5),
                                     vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is fresh, Pragma header with values other than "no-cache" is ignored.
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaNoFallback) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Pragma, "no-cache");
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "max-age=10");
  const LookupRequest lookup_request(request_headers_, currentTime() + Seconds(5),
                                     vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Pragma header is ignored when Cache-Control header is present.
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, SingleSatisfiableRange) {
  // add range info to headers
  request_headers_.addReference(Http::Headers::get().Range, "bytes=1-99");
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);

  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())},
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
  EXPECT_EQ(lookup_response.response_ranges_.size(), 1);

  EXPECT_EQ(lookup_response.response_ranges_[0].begin(), 1);
  EXPECT_EQ(lookup_response.response_ranges_[0].end(), 4);
  EXPECT_EQ(lookup_response.response_ranges_[0].length(), 3);

  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, MultipleSatisfiableRanges) {
  // Because we do not support multi-part responses for now, we are limiting parsing of a single
  // range. Thus, multiple ranges are ignored, and a usual "::Ok" should be expected. If multi-part
  // responses are implemented (and the parsing limit is changed), this test should be adjusted.

  // add range info to headers
  request_headers_.addCopy(Http::Headers::get().Range.get(), "bytes=1-99,3-,-3");

  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);

  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())},
       {"cache-control", "public, max-age=3600"},
       {"content-length", "4"}});
  const uint64_t content_length = 4;
  const LookupResult lookup_response =
      makeLookupResult(lookup_request, response_headers, content_length);

  ASSERT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);

  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(lookup_response.content_length_, 4);

  // Check that the ranges have been ignored since we don't support multi-part responses.
  EXPECT_EQ(lookup_response.response_ranges_.size(), 0);
  EXPECT_FALSE(lookup_response.has_trailers_);
}

TEST_F(LookupRequestTest, NotSatisfiableRange) {
  // add range info to headers
  request_headers_.addReference(Http::Headers::get().Range, "bytes=100-");

  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);

  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())},
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

TEST_F(LookupRequestTest, VariedHeaders) {
  request_headers_.addCopy("accept", "image/*");
  request_headers_.addCopy("other-header", "abc123");
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);
  const Http::RequestHeaderMap& result = lookup_request.getVaryHeaders();

  ASSERT_FALSE(result.get(Http::LowerCaseString("accept")).empty());
  ASSERT_EQ(result.get(Http::LowerCaseString("accept"))[0]->value().getStringView(), "image/*");
  ASSERT_TRUE(result.get(Http::LowerCaseString("other-header")).empty());
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
