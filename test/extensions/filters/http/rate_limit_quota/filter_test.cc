#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using ::envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;
using ::Envoy::Extensions::HttpFilters::RateLimitQuota::FilterConfig;
using Server::Configuration::MockFactoryContext;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using Upstream::MockThreadLocalCluster;

constexpr char BucketSetting[] = R"EOF(
    bucket_id_builder:
      bucket_id_builder:
        "name":
            string_value: "prod"
        "env":
            custom_value:
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: environment
        "group":
            custom_value:
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: group
  )EOF";

// constexpr char FilterConfig[] =  R"EOF(
//   matcher:
//     matcher_list:
//       matchers:
//         # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
//         predicate:
//           single_predicate:
//             input:
//               typed_config:
//                 "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
//                 header_name: env
//             value_match:
//               exact: staging
//         on_match:
//           action:
//             typed_config:
//               '@type':
//               type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
//               bucket_id_builder:
//                 bucket_id_builder:
//                   name:
//                     string_value: staging
//   )EOF";

// TODO(tyxia) matcher_list example https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/test/common/matcher/matcher_test.cc;rcl=442062708;l=162
constexpr char MatcherConfig[] = R"EOF(
    matcher_list:
      matchers:
        # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
        predicate:
          single_predicate:
            input:
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: env
            value_match:
              exact: staging
        on_match:
          action:
            name: rate_limit_quota
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
              bucket_id_builder:
                bucket_id_builder:
                  "name":
                      string_value: "prod"
                  "env":
                      custom_value:
                        typed_config:
                          "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                          header_name: environment
                  "group":
                      custom_value:
                        typed_config:
                          "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                          header_name: group
  )EOF";

class RateLimitQuotaFilterTest : public testing::Test {
public:
  RateLimitQuotaFilterTest() {

    xds::type::matcher::v3::Matcher matcher;
    TestUtility::loadFromYaml(MatcherConfig, matcher);
    config_.mutable_bucket_matchers()->MergeFrom(matcher);
    // TestUtility::loadFromYaml(FilterConfig, config_);
    filter_config_ = std::make_shared<FilterConfig>(config_);
    filter_ = std::make_unique<RateLimitQuotaFilter>(filter_config_, context_, nullptr);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  NiceMock<MockFactoryContext> context_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;

  std::unique_ptr<RateLimitQuotaFilter> filter_;
  FilterConfigConstSharedPtr filter_config_;
  FilterConfig config_;
};

TEST_F(RateLimitQuotaFilterTest, BucketSettings) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},         {":path", "/"},
                                         {":scheme", "http"},        {":authority", "host"},
                                         {"environment", "staging"}, {"group", "envoy"}};
  envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings
      bucket_setting;
  TestUtility::loadFromYaml(BucketSetting, bucket_setting);
  BucketId id = filter_->buildBucketsId(headers, bucket_setting);
  // TODO(tyxia) Maybe use matcher for testing purpose.
  for (auto it : id.bucket()) {
    std::cout << it.first << "___" << it.second << std::endl;
  }
  std::cout << "setting from filter config" << std::endl;
  auto settings = filter_->buildMatcher();
  for (auto bucket_setting : settings) {
    id = filter_->buildBucketsId(headers, bucket_setting);
    // TODO(tyxia) Maybe use matcher for testing purpose.
    for (auto it : id.bucket()) {
      std::cout << it.first << "___" << it.second << std::endl;
    }
  }
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
