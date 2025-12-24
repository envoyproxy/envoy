#include "source/extensions/filters/http/stream_to_metadata/config.h"
#include "source/extensions/filters/http/stream_to_metadata/filter.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StreamToMetadata {
namespace {

TEST(StreamToMetadataConfigTest, ValidConfig) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  StreamToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb_or.value()(filter_callback);
}

TEST(StreamToMetadataConfigTest, MultipleMetadataDescriptors) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
          - metadata_namespace: "envoy.audit"
            key: "token_count"
            type: NUMBER
            preserve_existing_metadata_value: true
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  StreamToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb_or.value()(filter_callback);
}

TEST(StreamToMetadataConfigTest, MultipleRules) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
      - selector:
          json_path:
            path: ["model"]
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "model_name"
            type: STRING
        stop_processing_on_match: false
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  StreamToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

TEST(StreamToMetadataConfigTest, CustomContentTypes) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["data"]
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "value"
    allowed_content_types:
      - "text/event-stream"
      - "application/stream+json"
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  StreamToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

TEST(StreamToMetadataConfigTest, EmptyConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  StreamToMetadataConfig factory;

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata empty_proto_config =
      *dynamic_cast<envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata*>(
          factory.createEmptyConfigProto().get());

  // Empty config should fail validation (no rules)
  EXPECT_THROW(TestUtility::validate(empty_proto_config), EnvoyException);
}

TEST(StreamToMetadataConfigTest, InvalidConfigMissingPath) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(StreamToMetadataConfigTest, InvalidConfigEmptyPath) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: []
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(StreamToMetadataConfigTest, InvalidConfigMissingNamespace) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage"]
        metadata_descriptors:
          - key: "tokens"
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(StreamToMetadataConfigTest, InvalidConfigMissingKey) {
  const std::string yaml = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage"]
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
  )EOF";

  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(StreamToMetadataConfigTest, InvalidConfigNoJsonPath) {
  // Create a config programmatically with an empty selector (not possible via YAML)
  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  auto* rules = proto_config.mutable_response_rules();
  rules->set_format(envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::
                        SERVER_SENT_EVENTS);

  auto* rule = rules->add_rules();
  rule->mutable_selector(); // Create empty selector (no json_path)

  auto* descriptor = rule->add_metadata_descriptors();
  descriptor->set_metadata_namespace("envoy.lb");
  descriptor->set_key("tokens");
  descriptor->set_type(
      envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::NUMBER);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  StreamToMetadataConfig factory;

  // Should fail because selector doesn't have json_path
  auto result = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Selector must have json_path specified"));
}

} // namespace
} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
