#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"

#include "source/extensions/filters/http/sse_to_metadata/config.h"
#include "source/extensions/filters/http/sse_to_metadata/filter.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SseToMetadata {
namespace {

TEST(SseToMetadataConfigTest, ValidConfig) {
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  SseToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb_or.value()(filter_callback);
}

TEST(SseToMetadataConfigTest, MultipleMetadataDescriptors) {
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.audit"
                key: "token_count"
                type: NUMBER
                preserve_existing_metadata_value: true
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  SseToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb_or.value()(filter_callback);
}

TEST(SseToMetadataConfigTest, MultipleRules) {
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "model"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "model_name"
                type: STRING
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  SseToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

TEST(SseToMetadataConfigTest, EmptyConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  SseToMetadataConfig factory;

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata empty_proto_config =
      *dynamic_cast<envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata*>(
          factory.createEmptyConfigProto().get());

  // Empty config should fail validation (no response_rules - required field)
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(empty_proto_config, "stats", context).IgnoreError(),
      EnvoyException, "Proto constraint validation failed.*value is required");
}

TEST(SseToMetadataConfigTest, InvalidConfigMissingPath) {
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Should fail during proto validation (selectors must have at least 1 item)
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SseToMetadataConfig factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
      EnvoyException, "Proto constraint validation failed.*Selectors.*at least 1 item");
}

TEST(SseToMetadataConfigTest, InvalidConfigEmptyPath) {
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors: []
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Should fail during proto validation (selectors must have at least 1 item)
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SseToMetadataConfig factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
      EnvoyException, "Proto constraint validation failed.*Selectors.*at least 1 item");
}

TEST(SseToMetadataConfigTest, EmptyNamespaceDefaultsToFilterName) {
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
              on_present:
                key: "tokens"
  )EOF";

  // Empty namespace is now valid - it defaults to filter name
  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  SseToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

TEST(SseToMetadataConfigTest, InvalidConfigMissingKey) {
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
              on_present:
                metadata_namespace: "envoy.lb"
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Should fail during proto validation or factory creation (missing required 'key' field)
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SseToMetadataConfig factory;
  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
               EnvoyException);
}

TEST(SseToMetadataConfigTest, InvalidConfigNoSelector) {
  // Create a config programmatically with an empty selector
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Should fail during proto validation (selectors must have at least 1 item)
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SseToMetadataConfig factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
      EnvoyException, "Proto constraint validation failed.*Selectors.*at least 1 item");
}

TEST(SseToMetadataConfigTest, RequiresAtLeastOneAction) {
  // Create config with no on_present, on_missing, or on_error
  const std::string yaml = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
  )EOF";

  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  SseToMetadataConfig factory;

  // Should fail because no on_present, on_missing, or on_error specified
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
      EnvoyException, "At least one of on_present, on_missing, or on_error must be specified");
}

} // namespace
} // namespace SseToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
