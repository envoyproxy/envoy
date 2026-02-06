#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"
#include "source/extensions/filters/http/bandwidth_limit/config.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

using EnableMode = envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit_EnableMode;
using StatusHelpers::HasStatus;
using testing::HasSubstr;
using testing::ReturnRef;

class FactoryTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  absl::StatusOr<std::shared_ptr<const FilterConfig>> configFromYaml(absl::string_view yaml) {
    BandwidthLimitFilterConfig factory;
    ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
    TestUtility::loadFromYaml(std::string{yaml}, *proto_config);
    const auto result = factory.createRouteSpecificFilterConfig(
        *proto_config, factory_context_.server_factory_context_,
        ProtobufMessage::getNullValidationVisitor());
    if (!result.ok()) {
      return result.status();
    }
    return std::dynamic_pointer_cast<const FilterConfig>(result.value());
  }

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
};

TEST_F(FactoryTest, GlobalEmptyConfig) {
  const std::string yaml = R"(
  stat_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  auto callback =
      factory.createFilterFactoryFromProto(*proto_config, "stats", factory_context_).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

TEST_F(FactoryTest, RouteSpecificFilterConfig) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 10
  fill_interval: 0.1s
  enable_response_trailers: true
  response_trailer_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context_.server_factory_context_,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->limit_kbps(), 10);
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 100);
  EXPECT_EQ(config->enableMode(), EnableMode::BandwidthLimit_EnableMode_REQUEST_AND_RESPONSE);
  EXPECT_FALSE(config->bucketAndStats(mock_stream_info_)->bucket() == nullptr);
  EXPECT_EQ(config->enableResponseTrailers(), true);
  EXPECT_EQ(const_cast<FilterConfig*>(config)->requestDelayTrailer(),
            Http::LowerCaseString("test-bandwidth-request-delay-ms"));
  EXPECT_EQ(const_cast<FilterConfig*>(config)->responseDelayTrailer(),
            Http::LowerCaseString("test-bandwidth-response-delay-ms"));
}

TEST_F(FactoryTest, RouteSpecificFilterConfigDisabledByDefault) {
  const std::string config_yaml = R"(
  stat_prefix: test
  limit_kbps: 10
  fill_interval: 0.1s
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context_.server_factory_context_,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->enableMode(), EnableMode::BandwidthLimit_EnableMode_DISABLED);
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->limit_kbps(), 10);
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 100);
}

TEST_F(FactoryTest, RouteSpecificFilterConfigDefault) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 10
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context_.server_factory_context_,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->limit_kbps(), 10);
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 50);
  // default trailers
  EXPECT_EQ(config->enableResponseTrailers(), false);
  EXPECT_EQ(const_cast<FilterConfig*>(config)->requestDelayTrailer(),
            Http::LowerCaseString("bandwidth-request-delay-ms"));
  EXPECT_EQ(const_cast<FilterConfig*>(config)->responseDelayTrailer(),
            Http::LowerCaseString("bandwidth-response-delay-ms"));
}

TEST_F(FactoryTest, PerRouteConfigNoLimits) {
  const std::string config_yaml = R"(
  stat_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  const auto result = factory.createRouteSpecificFilterConfig(
      *proto_config, factory_context_.server_factory_context_,
      ProtobufMessage::getNullValidationVisitor());
  EXPECT_EQ(result.status().message(), "limit must be set for per route filter config");
}

TEST_F(FactoryTest, FixedNameBucketSelectorUsesNamedBucket) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector:
    explicit_bucket: test_explicit_bucket
  named_bucket_configurations:
  - name: test_explicit_bucket
    limit_kbps: 100
    fill_interval: 0.02s
  )")
                    .value();
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 20);
}

TEST_F(FactoryTest, DuplicateNamedBucketConfigurationsIsAnError) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector:
    explicit_bucket: test_explicit_bucket
  named_bucket_configurations:
  - name: test_explicit_bucket
    limit_kbps: 100
    fill_interval: 0.02s
  - name: test_explicit_bucket
    limit_kbps: 101
    fill_interval: 0.03s
  )");
  EXPECT_THAT(config,
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("duplicate bucket name")));
}

TEST_F(FactoryTest, FixedNameBucketSelectorCreatesDefaultBucket) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  fill_interval: 0.03s
  named_bucket_selector:
    explicit_bucket: test_explicit_bucket
    create_bucket_if_not_existing: true
  )")
                    .value();
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 30);
}

TEST_F(FactoryTest, NamedBucketSelectorSelectingNonexistentBucketWithNoDefaultReturnsNullopt) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector:
    explicit_bucket: test_explicit_bucket
  )")
                    .value();
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_), absl::nullopt);
}

TEST_F(FactoryTest, NamedBucketSelectorCreatingDefaultBucketRequiresDefaultConfig) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  named_bucket_selector:
    explicit_bucket: test_explicit_bucket
    create_bucket_if_not_existing: true
  )");
  EXPECT_THAT(config,
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("limit_kbps must be set")));
}

TEST_F(FactoryTest, NamedBucketSelectorMustHaveType) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector: {}
  )");
  EXPECT_THAT(config, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("precisely one of")));
}

TEST_F(FactoryTest, NamedBucketSelectorMustHaveOnlyOneType) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector:
    explicit_bucket: "beep"
    client_cn_with_default: {}
  )");
  EXPECT_THAT(config, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("precisely one of")));
}

TEST_F(FactoryTest, ClientCnBucketSelectorUsesCertificateNameOrDefaultBucket) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector:
    client_cn_with_default:
      default_bucket: "no_cn_bucket"
  named_bucket_configurations:
  - name: no_cn_bucket
    limit_kbps: 100
    fill_interval: 0.25s
  - name: example_cn
    limit_kbps: 100
    fill_interval: 0.26s
  )")
                    .value();
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  std::string no_cn;
  std::string example_cn = "example_cn";
  EXPECT_CALL(*ssl, subjectPeerCertificate)
      .WillOnce(ReturnRef(no_cn))
      .WillOnce(ReturnRef(example_cn));
  // No SSL connection uses default bucket.
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 250);
  mock_stream_info_.downstream_connection_info_provider_->setSslConnection(ssl);
  // No CN uses default bucket.
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 250);
  // example_cn uses example_cn bucket.
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 260);
}

TEST_F(FactoryTest, ClientCnBucketSelectorUsesCertificateNameWithTemplate) {
  auto config = configFromYaml(R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector:
    client_cn_with_default:
      default_bucket: "no_cn_bucket"
      name_template: "prefix_{CN}_postfix"
  named_bucket_configurations:
  - name: no_cn_bucket
    limit_kbps: 100
    fill_interval: 0.25s
  - name: prefix_example_cn_postfix
    limit_kbps: 100
    fill_interval: 0.26s
  )")
                    .value();
  // No SSL connection uses default bucket without templating.
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 250);
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  std::string example_cn = "example_cn";
  EXPECT_CALL(*ssl, subjectPeerCertificate).WillOnce(ReturnRef(example_cn));
  mock_stream_info_.downstream_connection_info_provider_->setSslConnection(ssl);
  // example_cn uses prefix_example_cn_postfix bucket due to name_template.
  EXPECT_THAT(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 260);
}

TEST_F(FactoryTest, GlobalConfigWithInvalidNamedBucketSelectorReturnsErrorStatus) {
  const std::string yaml = R"(
  stat_prefix: test
  named_bucket_selector: {}
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", factory_context_);
  EXPECT_THAT(result, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("precisely one")));
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
