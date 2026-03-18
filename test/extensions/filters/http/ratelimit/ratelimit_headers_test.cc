#include <cstdint>
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
  Http::TestResponseHeaderMapImpl expected_headers_when_disabled_by_default;
  DescriptorStatusList descriptor_statuses;
  std::vector<Envoy::RateLimit::Descriptor> descriptors;
};

class RateLimitHeadersTest : public testing::TestWithParam<RateLimitHeadersTestCase> {
public:
  static const std::vector<RateLimitHeadersTestCase>& getTestCases() {
    CONSTRUCT_ON_FIRST_USE(
        std::vector<RateLimitHeadersTestCase>,
        // Empty descriptor statuses
        {{}, {}, {}, {}},
        // Status with no current limit is ignored
        {
            {{"x-ratelimit-limit", "4, 4;w=3600;name=\"second\""},
             {"x-ratelimit-remaining", "5"},
             {"x-ratelimit-reset", "6"}},
            {},
            {
                // passing 0 will cause it not to set a current limit
                buildDescriptorStatus(
                    0, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first",
                    2, 3),
                buildDescriptorStatus(
                    4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second",
                    5, 6),
            },
            {
                Envoy::RateLimit::Descriptor(),
                Envoy::RateLimit::Descriptor(),
            },
        },
        // Empty name is not appended
        {
            {{"x-ratelimit-limit", "1, 1;w=60"},
             {"x-ratelimit-remaining", "2"},
             {"x-ratelimit-reset", "3"}},
            {},
            {
                buildDescriptorStatus(
                    1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "", 2,
                    3),
            },
            {
                Envoy::RateLimit::Descriptor(),
            },
        },
        // Unknown unit is ignored in window, but not overall
        {
            {{"x-ratelimit-limit", "1, 4;w=3600;name=\"second\""},
             {"x-ratelimit-remaining", "2"},
             {"x-ratelimit-reset", "3"}},
            {},
            {buildDescriptorStatus(
                 1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::UNKNOWN, "first",
                 2, 3),
             buildDescriptorStatus(
                 4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5,
                 6)},
            {
                Envoy::RateLimit::Descriptor(),
                Envoy::RateLimit::Descriptor(),
            },
        },
        // Normal case, multiple arguments
        {
            {{"x-ratelimit-limit", "1, 1;w=60;name=\"first\", 4;w=3600;name=\"second\""},
             {"x-ratelimit-remaining", "2"},
             {"x-ratelimit-reset", "3"}},
            {},
            {buildDescriptorStatus(
                 1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first", 2,
                 3),
             buildDescriptorStatus(
                 4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5,
                 6)},
            {
                Envoy::RateLimit::Descriptor(),
                Envoy::RateLimit::Descriptor(),
            },
        },
        // Normal case but the descriptor with min remaining limit disabled the headers
        {
            {},
            {},
            {buildDescriptorStatus(
                 1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first", 2,
                 3),
             buildDescriptorStatus(
                 4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5,
                 6)},
            {
                Envoy::RateLimit::Descriptor{
                    .entries_ = {},
                    .x_ratelimit_option_ = RateLimit::RateLimitProto::OFF,
                },
                Envoy::RateLimit::Descriptor(),
            },
        },
        // Normal case but one of the descriptors disabled the headers
        // This case should still populate the headers since the descriptor with
        // min remaining limit did not disable it. But the disabled descriptor will be be skipped
        // for quota policy population.
        {
            {{"x-ratelimit-limit", "1, 1;w=60;name=\"first\""},
             {"x-ratelimit-remaining", "2"},
             {"x-ratelimit-reset", "3"}},
            {},
            {buildDescriptorStatus(
                 1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first", 2,
                 3),
             buildDescriptorStatus(
                 4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5,
                 6)},
            {
                Envoy::RateLimit::Descriptor(),
                Envoy::RateLimit::Descriptor{
                    .entries_ = {},
                    .x_ratelimit_option_ = RateLimit::RateLimitProto::OFF,
                },
            },
        },
        // Normal case but one of the descriptors enabled the headers explicitly and one
        // disabled it explicitly.
        {
            {{"x-ratelimit-limit", "1, 1;w=60;name=\"first\""},
             {"x-ratelimit-remaining", "2"},
             {"x-ratelimit-reset", "3"}},
            {{"x-ratelimit-limit", "1, 1;w=60;name=\"first\""},
             {"x-ratelimit-remaining", "2"},
             {"x-ratelimit-reset", "3"}},
            {buildDescriptorStatus(
                 1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first", 2,
                 3),
             buildDescriptorStatus(
                 4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5,
                 6)},
            {
                Envoy::RateLimit::Descriptor{
                    .entries_ = {},
                    .x_ratelimit_option_ = RateLimit::RateLimitProto::DRAFT_VERSION_03,
                },
                Envoy::RateLimit::Descriptor{
                    .entries_ = {},
                    .x_ratelimit_option_ = RateLimit::RateLimitProto::OFF,
                },
            },
        },
        // Normal case but with unmatched descriptors and statuses.
        {
            {{"x-ratelimit-limit", "1, 1;w=60;name=\"first\", 4;w=3600;name=\"second\""},
             {"x-ratelimit-remaining", "2"},
             {"x-ratelimit-reset", "3"}},
            {},
            {buildDescriptorStatus(
                 1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first", 2,
                 3),
             buildDescriptorStatus(
                 4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5,
                 6)},
            {},
        }, );
  }
};

INSTANTIATE_TEST_SUITE_P(RateLimitHeadersTest, RateLimitHeadersTest,
                         testing::ValuesIn(RateLimitHeadersTest::getTestCases()));

TEST_P(RateLimitHeadersTest, RateLimitHeadersTest) {
  Http::TestResponseHeaderMapImpl headers;
  XRateLimitHeaderUtils::populateHeaders(GetParam().descriptors, /*enabled=*/true,
                                         GetParam().descriptor_statuses, headers);
  EXPECT_THAT(&headers, HeaderMapEqual(&GetParam().expected_headers));
  headers.clear();
  XRateLimitHeaderUtils::populateHeaders(GetParam().descriptors, /*enabled=*/false,
                                         GetParam().descriptor_statuses, headers);
  EXPECT_THAT(&headers, HeaderMapEqual(&GetParam().expected_headers_when_disabled_by_default));
}

TEST_P(RateLimitHeadersTest, TestUintConversions) {
  const absl::flat_hash_map<envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::Unit,
                            uint32_t>
      unit_map = {
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::SECOND, 1},
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, 60},
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, 3600},
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::DAY, 86400},
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::WEEK, 604800},
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MONTH, 2592000},
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::YEAR, 31536000},
          {envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::UNKNOWN, 0},
      };

  for (const auto& [unit_enum, expected_seconds] : unit_map) {
    EXPECT_EQ(XRateLimitHeaderUtils::convertRateLimitUnit(unit_enum), expected_seconds);
  }
}

} // namespace
} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
