#include "source/extensions/sse_content_parsers/json/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace SseContentParsers {
namespace Json {
namespace {

TEST(JsonContentParserConfigTest, ValidConfig) {
  const std::string yaml = R"EOF(
  rules:
    - selectors:
        - key: "usage"
        - key: "total_tokens"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "tokens"
        type: NUMBER
  )EOF";

  envoy::extensions::sse_content_parsers::json::v3::JsonContentParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  auto parser_factory = factory.createParserFactory(proto_config, context);
  EXPECT_NE(parser_factory, nullptr);
}

TEST(JsonContentParserConfigTest, OnMissingWithEmptyValue) {
  // Test that on_missing with explicit value but empty value throws exception
  const std::string yaml = R"EOF(
  rules:
    - selectors:
        - key: "usage"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "tokens"
        type: NUMBER
      on_missing:
        metadata_namespace: "envoy.lb"
        key: "missing"
        value: {}
  )EOF";

  envoy::extensions::sse_content_parsers::json::v3::JsonContentParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createParserFactory(proto_config, context), EnvoyException,
                            "on_missing KeyValuePair with explicit value must have value set");
}

TEST(JsonContentParserConfigTest, OnErrorWithEmptyValue) {
  // Test that on_error with explicit value but empty value throws exception
  const std::string yaml = R"EOF(
  rules:
    - selectors:
        - key: "usage"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "tokens"
        type: NUMBER
      on_error:
        metadata_namespace: "envoy.lb"
        key: "error"
        value: {}
  )EOF";

  envoy::extensions::sse_content_parsers::json::v3::JsonContentParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createParserFactory(proto_config, context), EnvoyException,
                            "on_error KeyValuePair with explicit value must have value set");
}

TEST(JsonContentParserConfigTest, RequiresAtLeastOneAction) {
  // Test that at least one of on_present, on_missing, or on_error must be specified
  const std::string yaml = R"EOF(
  rules:
    - selectors:
        - key: "usage"
  )EOF";

  envoy::extensions::sse_content_parsers::json::v3::JsonContentParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createParserFactory(proto_config, context), EnvoyException,
      "At least one of on_present, on_missing, or on_error must be specified");
}

} // namespace
} // namespace Json
} // namespace SseContentParsers
} // namespace Extensions
} // namespace Envoy
