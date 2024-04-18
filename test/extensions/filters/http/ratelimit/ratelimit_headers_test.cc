#include <string>
#include <vector>

#include "source/extensions/filters/http/ratelimit/ratelimit_headers.h"

#include "test/extensions/filters/common/ratelimit/utils.h"
#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {
namespace {

using Envoy::RateLimit::buildDescriptorStatus;
using Filters::Common::RateLimit::DescriptorStatusList;

struct RateLimitHeadersTestCase {
  Http::TestResponseHeaderMapImpl expected_headers;
  DescriptorStatusList descriptor_statuses;
};

class RateLimitHeadersTest : public testing::TestWithParam<RateLimitHeadersTestCase> {
public:
  static const std::vector<RateLimitHeadersTestCase>& getTestCases() {
    CONSTRUCT_ON_FIRST_USE(
        std::vector<RateLimitHeadersTestCase>,
        // Empty descriptor statuses
        {{}, {}},
        // Status with no current limit is ignored
        {{{"x-ratelimit-limit", "4, 4;w=3600;name=\"second\""},
          {"x-ratelimit-remaining", "5"},
          {"x-ratelimit-reset", "6"}},
         {// passing 0 will cause it not to set a current limit
          buildDescriptorStatus(0,
                                envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE,
                                "first", 2, 3),
          buildDescriptorStatus(4,
                                envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR,
                                "second", 5, 6)}},
        // Empty name is not appended
        {{{"x-ratelimit-limit", "1, 1;w=60"},
          {"x-ratelimit-remaining", "2"},
          {"x-ratelimit-reset", "3"}},
         {
             // passing 0 will cause it not to set a current limit
             buildDescriptorStatus(
                 1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "", 2, 3),
         }},
        // Unknown unit is ignored in window, but not overall
        {{{"x-ratelimit-limit", "1, 4;w=3600;name=\"second\""},
          {"x-ratelimit-remaining", "2"},
          {"x-ratelimit-reset", "3"}},
         {// passing 0 will cause it not to set a current limit
          buildDescriptorStatus(
              1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::UNKNOWN, "first", 2,
              3),
          buildDescriptorStatus(4,
                                envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR,
                                "second", 5, 6)}},
        // Normal case, multiple arguments
        {{{"x-ratelimit-limit", "1, 1;w=60;name=\"first\", 4;w=3600;name=\"second\""},
          {"x-ratelimit-remaining", "2"},
          {"x-ratelimit-reset", "3"}},
         {buildDescriptorStatus(1,
                                envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE,
                                "first", 2, 3),
          buildDescriptorStatus(4,
                                envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR,
                                "second", 5, 6)}}, );
  }
};

INSTANTIATE_TEST_SUITE_P(RateLimitHeadersTest, RateLimitHeadersTest,
                         testing::ValuesIn(RateLimitHeadersTest::getTestCases()));

TEST_P(RateLimitHeadersTest, RateLimitHeadersTest) {
  Http::ResponseHeaderMapPtr result = XRateLimitHeaderUtils::create(
      std::make_unique<DescriptorStatusList>(GetParam().descriptor_statuses));
  EXPECT_THAT(result, HeaderMapEqual(&GetParam().expected_headers));
}

} // namespace
} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
