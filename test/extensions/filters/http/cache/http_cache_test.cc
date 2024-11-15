#include <chrono>
#include <string>

#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/http_cache.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::TestWithParam;

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

envoy::extensions::filters::http::cache::v3::CacheConfig getConfig() {
  // Allows 'accept' to be varied in the tests.
  envoy::extensions::filters::http::cache::v3::CacheConfig config;
  const auto& add_accept = config.mutable_allowed_vary_headers()->Add();
  add_accept->set_exact("accept");
  return config;
}

class LookupRequestTest : public testing::TestWithParam<LookupRequestTestCase> {
public:
  LookupRequestTest() : vary_allow_list_(getConfig().allowed_vary_headers(), factory_context_) {}

  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {":scheme", "https"}, {":authority", "example.com"}};

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  VaryAllowList vary_allow_list_;

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
                              absl::optional<uint64_t> content_length = absl::nullopt) {
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
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers, 0);

  EXPECT_EQ(GetParam().expected_cache_entry_status, lookup_response.cache_entry_status_);
  ASSERT_TRUE(lookup_response.headers_);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  EXPECT_THAT(*lookup_response.headers_,
              HeaderHasValueRef(Http::CustomHeaders::get().Age, GetParam().expected_age));
  EXPECT_EQ(lookup_response.content_length_, 0);
}

TEST_P(LookupRequestTest, ResultWithUnknownContentLengthMatchesExpectation) {
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
              HeaderHasValueRef(Http::CustomHeaders::get().Age, GetParam().expected_age));
  EXPECT_FALSE(lookup_response.content_length_.has_value());
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
              HeaderHasValueRef(Http::CustomHeaders::get().Age, GetParam().expected_age));
  EXPECT_EQ(lookup_response.content_length_, content_length);
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
  // Response is not expired but the request requires revalidation through
  // Pragma: no-cache.
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

// "pragma:no-cache" is ignored if ignoreRequestCacheControlHeader is true.
TEST_F(LookupRequestTest, IgnoreRequestCacheControlHeaderIgnoresPragma) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Pragma, "no-cache");
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_,
                                     /*ignore_request_cache_control_header=*/true);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is not expired and no-cache is ignored.
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

// "cache-control:no-cache" is ignored if ignoreRequestCacheControlHeader is true.
TEST_F(LookupRequestTest, IgnoreRequestCacheControlHeaderIgnoresCacheControl) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().CacheControl, "no-cache");
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_,
                                     /*ignore_request_cache_control_header=*/true);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is not expired and no-cache is ignored.
  EXPECT_EQ(CacheEntryStatus::Ok, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaNoCacheFallbackExtraDirectivesIgnored) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Pragma,
                                   "no-cache, custom-directive=custom-value");
  const LookupRequest lookup_request(request_headers_, currentTime(), vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is not expired but the request requires revalidation through
  // Pragma: no-cache.
  EXPECT_EQ(CacheEntryStatus::RequiresValidation, lookup_response.cache_entry_status_);
}

TEST_F(LookupRequestTest, PragmaFallbackOtherValuesIgnored) {
  request_headers_.setReferenceKey(Http::CustomHeaders::get().Pragma, "max-age=0");
  const LookupRequest lookup_request(request_headers_, currentTime() + Seconds(5),
                                     vary_allow_list_);
  const Http::TestResponseHeaderMapImpl response_headers(
      {{"date", formatter_.fromTime(currentTime())}, {"cache-control", "public, max-age=3600"}});
  const LookupResult lookup_response = makeLookupResult(lookup_request, response_headers);
  // Response is fresh, Pragma header with values other than "no-cache" is
  // ignored.
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

TEST(HttpCacheTest, StableHashKey) {
  Key key;
  key.set_host("example.com");
  ASSERT_EQ(stableHashKey(key), 6153940628716543519u);
}

TEST_P(LookupRequestTest, ResultWithBodyAndTrailersMatchesExpectation) {
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
  ASSERT_TRUE(lookup_response.headers_ != nullptr);
  EXPECT_THAT(*lookup_response.headers_, Http::IsSupersetOfHeaders(response_headers));
  // Age is populated in LookupRequest::makeLookupResult, which is called in makeLookupResult.
  EXPECT_THAT(*lookup_response.headers_,
              HeaderHasValueRef(Http::CustomHeaders::get().Age, GetParam().expected_age));
  EXPECT_EQ(lookup_response.content_length_, content_length);
}

TEST_F(LookupRequestTest, HttpScheme) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":scheme", "http"}, {":authority", "example.com"}};
  const LookupRequest lookup_request(request_headers, currentTime(), vary_allow_list_);
  EXPECT_EQ(lookup_request.key().scheme(), Key::HTTP);
}

TEST_F(LookupRequestTest, HttpsScheme) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {":method", "GET"}, {":scheme", "https"}, {":authority", "example.com"}};
  const LookupRequest lookup_request(request_headers, currentTime(), vary_allow_list_);
  EXPECT_EQ(lookup_request.key().scheme(), Key::HTTPS);
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
