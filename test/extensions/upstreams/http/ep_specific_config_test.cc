#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.validate.h"

#include "source/extensions/upstreams/http/ep_specific_config.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace {

using ::testing::NiceMock;

class EpSpecificConfigTest : public ::testing::Test {
public:
  envoy::extensions::upstreams::http::v3::EndpointSpecificHttpProtocolOptions options_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
};

// Test that an empty config can be created.
TEST_F(EpSpecificConfigTest, EmptyConfig) {
  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  EXPECT_TRUE(config->compiledOptions().empty());
}

// Test parsing a single endpoint-specific option with max_concurrent_streams.
TEST_F(EpSpecificConfigTest, SingleEndpointWithHttp2MaxConcurrentStreams) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 200
    endpoint_metadata_match:
      filter: ep_specific_protocol_options
      path:
        - key: type
      value:
        string_match:
          exact: "high_throughput"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  EXPECT_EQ(1, config->compiledOptions().size());

  const auto& ep_option = config->compiledOptions()[0];
  EXPECT_TRUE(ep_option.http2_protocol_options.has_value());
  EXPECT_EQ(200, ep_option.http2_protocol_options->max_concurrent_streams().value());
  EXPECT_TRUE(ep_option.metadata_matcher.has_value());
}

// Test parsing an endpoint-specific option with max_requests_per_connection.
TEST_F(EpSpecificConfigTest, SingleEndpointWithMaxRequestsPerConnection) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http_protocol_options:
      max_requests_per_connection: 5000
    endpoint_metadata_match:
      filter: ep_specific_protocol_options
      path:
        - key: rate_limit_tier
      value:
        string_match:
          exact: "premium"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  EXPECT_EQ(1, config->compiledOptions().size());

  const auto& ep_option = config->compiledOptions()[0];
  EXPECT_TRUE(ep_option.http_protocol_options.has_value());
  EXPECT_EQ(5000, ep_option.http_protocol_options->max_requests_per_connection().value());
  EXPECT_TRUE(ep_option.metadata_matcher.has_value());
}

// Test parsing multiple endpoint-specific options.
TEST_F(EpSpecificConfigTest, MultipleEndpoints) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 500
    http_protocol_options:
      max_requests_per_connection: 10000
    endpoint_metadata_match:
      filter: ep_specific_protocol_options
      path:
        - key: tier
      value:
        string_match:
          exact: "high_capacity"
  - http2_protocol_options:
      max_concurrent_streams: 50
    http_protocol_options:
      max_requests_per_connection: 1000
    endpoint_metadata_match:
      filter: ep_specific_protocol_options
      path:
        - key: tier
      value:
        string_match:
          exact: "low_capacity"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  EXPECT_EQ(2, config->compiledOptions().size());

  // First option: high_capacity tier
  const auto& high_capacity_option = config->compiledOptions()[0];
  EXPECT_TRUE(high_capacity_option.http2_protocol_options.has_value());
  EXPECT_EQ(500, high_capacity_option.http2_protocol_options->max_concurrent_streams().value());
  EXPECT_TRUE(high_capacity_option.http_protocol_options.has_value());
  EXPECT_EQ(10000, high_capacity_option.http_protocol_options->max_requests_per_connection().value());
  EXPECT_TRUE(high_capacity_option.metadata_matcher.has_value());

  // Second option: low_capacity tier
  const auto& low_capacity_option = config->compiledOptions()[1];
  EXPECT_TRUE(low_capacity_option.http2_protocol_options.has_value());
  EXPECT_EQ(50, low_capacity_option.http2_protocol_options->max_concurrent_streams().value());
  EXPECT_TRUE(low_capacity_option.http_protocol_options.has_value());
  EXPECT_EQ(1000, low_capacity_option.http_protocol_options->max_requests_per_connection().value());
  EXPECT_TRUE(low_capacity_option.metadata_matcher.has_value());
}

// Test that metadata matcher correctly matches endpoint metadata.
TEST_F(EpSpecificConfigTest, MetadataMatcherMatchesCorrectly) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 300
    endpoint_metadata_match:
      filter: envoy.lb
      path:
        - key: endpoint_type
      value:
        string_match:
          exact: "special"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  ASSERT_EQ(1, config->compiledOptions().size());

  const auto& ep_option = config->compiledOptions()[0];
  ASSERT_TRUE(ep_option.metadata_matcher.has_value());

  // Create metadata that matches
  envoy::config::core::v3::Metadata matching_metadata;
  (*matching_metadata.mutable_filter_metadata())["envoy.lb"]
      .mutable_fields()
      ->insert({"endpoint_type", ValueUtil::stringValue("special")});
  EXPECT_TRUE(ep_option.metadata_matcher->match(matching_metadata));

  // Create metadata that doesn't match
  envoy::config::core::v3::Metadata non_matching_metadata;
  (*non_matching_metadata.mutable_filter_metadata())["envoy.lb"]
      .mutable_fields()
      ->insert({"endpoint_type", ValueUtil::stringValue("regular")});
  EXPECT_FALSE(ep_option.metadata_matcher->match(non_matching_metadata));

  // Create metadata with missing filter
  envoy::config::core::v3::Metadata missing_filter_metadata;
  (*missing_filter_metadata.mutable_filter_metadata())["other_filter"]
      .mutable_fields()
      ->insert({"endpoint_type", ValueUtil::stringValue("special")});
  EXPECT_FALSE(ep_option.metadata_matcher->match(missing_filter_metadata));

  // Create empty metadata
  envoy::config::core::v3::Metadata empty_metadata;
  EXPECT_FALSE(ep_option.metadata_matcher->match(empty_metadata));
}

// Test metadata matcher with nested path matching.
TEST_F(EpSpecificConfigTest, MetadataMatcherNestedPath) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 250
    endpoint_metadata_match:
      filter: custom_filter
      path:
        - key: config
        - key: settings
        - key: mode
      value:
        string_match:
          exact: "optimized"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  ASSERT_EQ(1, config->compiledOptions().size());

  const auto& ep_option = config->compiledOptions()[0];
  ASSERT_TRUE(ep_option.metadata_matcher.has_value());

  // Create nested metadata that matches
  envoy::config::core::v3::Metadata matching_metadata;
  auto& filter_metadata =
      (*matching_metadata.mutable_filter_metadata())["custom_filter"];
  
  Protobuf::Struct settings;
  (*settings.mutable_fields())["mode"] = ValueUtil::stringValue("optimized");
  
  Protobuf::Struct config_struct;
  (*config_struct.mutable_fields())["settings"].mutable_struct_value()->CopyFrom(settings);
  
  (*filter_metadata.mutable_fields())["config"].mutable_struct_value()->CopyFrom(config_struct);
  
  EXPECT_TRUE(ep_option.metadata_matcher->match(matching_metadata));
}

// Test endpoint option with only HTTP/2 options specified.
TEST_F(EpSpecificConfigTest, OnlyHttp2OptionsSpecified) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 100
      initial_stream_window_size: 65536
    endpoint_metadata_match:
      filter: test
      path:
        - key: key
      value:
        string_match:
          exact: "value"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  ASSERT_EQ(1, config->compiledOptions().size());

  const auto& ep_option = config->compiledOptions()[0];
  EXPECT_TRUE(ep_option.http2_protocol_options.has_value());
  EXPECT_FALSE(ep_option.http_protocol_options.has_value());
  EXPECT_EQ(100, ep_option.http2_protocol_options->max_concurrent_streams().value());
  EXPECT_EQ(65536, ep_option.http2_protocol_options->initial_stream_window_size().value());
}

// Test endpoint option with only HTTP protocol options specified.
TEST_F(EpSpecificConfigTest, OnlyHttpProtocolOptionsSpecified) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http_protocol_options:
      idle_timeout: 300s
      max_requests_per_connection: 2000
    endpoint_metadata_match:
      filter: test
      path:
        - key: key
      value:
        string_match:
          exact: "value"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  ASSERT_EQ(1, config->compiledOptions().size());

  const auto& ep_option = config->compiledOptions()[0];
  EXPECT_FALSE(ep_option.http2_protocol_options.has_value());
  EXPECT_TRUE(ep_option.http_protocol_options.has_value());
  EXPECT_EQ(2000, ep_option.http_protocol_options->max_requests_per_connection().value());
  EXPECT_EQ(300, DurationUtil::durationToSeconds(ep_option.http_protocol_options->idle_timeout()));
}

// Test factory creates config from proto message correctly.
TEST_F(EpSpecificConfigTest, FactoryCreatesConfig) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 400
    endpoint_metadata_match:
      filter: test
      path:
        - key: key
      value:
        string_match:
          exact: "value"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  EpSpecificProtocolOptionsConfigFactory factory;
  EXPECT_EQ("envoy.extensions.upstreams.http.v3.EndpointSpecificHttpProtocolOptions", factory.name());
  EXPECT_EQ("envoy.upstream_options", factory.category());
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_NE(nullptr, factory.createEmptyProtocolOptionsProto());
}

// Test multiple metadata matchers with different filters.
TEST_F(EpSpecificConfigTest, DifferentFiltersInMatchers) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 100
    endpoint_metadata_match:
      filter: envoy.lb
      path:
        - key: zone
      value:
        string_match:
          exact: "zone-a"
  - http2_protocol_options:
      max_concurrent_streams: 200
    endpoint_metadata_match:
      filter: custom.routing
      path:
        - key: priority
      value:
        string_match:
          exact: "high"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  ASSERT_EQ(2, config->compiledOptions().size());

  // Test first option matches zone-a
  const auto& first_option = config->compiledOptions()[0];
  envoy::config::core::v3::Metadata zone_a_metadata;
  (*zone_a_metadata.mutable_filter_metadata())["envoy.lb"]
      .mutable_fields()
      ->insert({"zone", ValueUtil::stringValue("zone-a")});
  EXPECT_TRUE(first_option.metadata_matcher->match(zone_a_metadata));

  // Test second option matches high priority
  const auto& second_option = config->compiledOptions()[1];
  envoy::config::core::v3::Metadata high_priority_metadata;
  (*high_priority_metadata.mutable_filter_metadata())["custom.routing"]
      .mutable_fields()
      ->insert({"priority", ValueUtil::stringValue("high")});
  EXPECT_TRUE(second_option.metadata_matcher->match(high_priority_metadata));
}

// Test prefix match in metadata matcher.
TEST_F(EpSpecificConfigTest, PrefixMatchInMetadataMatcher) {
  const std::string yaml = R"EOF(
endpoint_specific_options:
  - http2_protocol_options:
      max_concurrent_streams: 150
    endpoint_metadata_match:
      filter: envoy.lb
      path:
        - key: datacenter
      value:
        string_match:
          prefix: "us-"
)EOF";
  TestUtility::loadFromYamlAndValidate(yaml, options_);

  auto config = std::make_shared<EpSpecificProtocolOptionsConfigImpl>(options_, server_context_);
  ASSERT_EQ(1, config->compiledOptions().size());

  const auto& ep_option = config->compiledOptions()[0];
  ASSERT_TRUE(ep_option.metadata_matcher.has_value());

  // Matches: us-east
  envoy::config::core::v3::Metadata us_east_metadata;
  (*us_east_metadata.mutable_filter_metadata())["envoy.lb"]
      .mutable_fields()
      ->insert({"datacenter", ValueUtil::stringValue("us-east")});
  EXPECT_TRUE(ep_option.metadata_matcher->match(us_east_metadata));

  // Matches: us-west
  envoy::config::core::v3::Metadata us_west_metadata;
  (*us_west_metadata.mutable_filter_metadata())["envoy.lb"]
      .mutable_fields()
      ->insert({"datacenter", ValueUtil::stringValue("us-west")});
  EXPECT_TRUE(ep_option.metadata_matcher->match(us_west_metadata));

  // Does not match: eu-west
  envoy::config::core::v3::Metadata eu_west_metadata;
  (*eu_west_metadata.mutable_filter_metadata())["envoy.lb"]
      .mutable_fields()
      ->insert({"datacenter", ValueUtil::stringValue("eu-west")});
  EXPECT_FALSE(ep_option.metadata_matcher->match(eu_west_metadata));
}

} // namespace
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
