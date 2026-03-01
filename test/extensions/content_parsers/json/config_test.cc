#include "source/extensions/content_parsers/json/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ContentParsers {
namespace Json {
namespace {

TEST(JsonContentParserConfigTest, ValidConfig) {
  const std::string yaml = R"EOF(
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

  envoy::extensions::content_parsers::json::v3::JsonContentParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  auto parser_factory = factory.createParserFactory(proto_config, context);
  EXPECT_NE(parser_factory, nullptr);

  // Test factory methods
  auto parser = parser_factory->createParser();
  EXPECT_NE(parser, nullptr);
  EXPECT_EQ(parser_factory->statsPrefix(), "json.");
}

TEST(JsonContentParserConfigTest, OnPresentWithEmptyValue) {
  envoy::extensions::content_parsers::json::v3::JsonContentParser proto_config;

  auto* rule_config = proto_config.add_rules();
  auto* rule = rule_config->mutable_rule();
  auto* selector = rule->add_selectors();
  selector->set_key("usage");

  // Set on_present with explicit value field but leave the Value empty (kind_case == 0)
  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->mutable_value(); // This sets the value oneof but leaves Value empty

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createParserFactory(proto_config, context), EnvoyException,
                            "on_present KeyValuePair with explicit value must have value set");
}

TEST(JsonContentParserConfigTest, OnMissingWithEmptyValue) {
  envoy::extensions::content_parsers::json::v3::JsonContentParser proto_config;

  auto* rule_config = proto_config.add_rules();
  auto* rule = rule_config->mutable_rule();
  auto* selector = rule->add_selectors();
  selector->set_key("usage");

  // Set on_present (required to pass the "at least one action" validation)
  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->set_type(
      envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::NUMBER);

  // Set on_missing with explicit value field but leave the Value empty (kind_case == 0)
  auto* on_missing = rule->mutable_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("missing");
  on_missing->mutable_value(); // This sets the value oneof but leaves Value empty

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createParserFactory(proto_config, context), EnvoyException,
                            "on_missing KeyValuePair with explicit value must have value set");
}

TEST(JsonContentParserConfigTest, OnErrorWithEmptyValue) {
  envoy::extensions::content_parsers::json::v3::JsonContentParser proto_config;

  auto* rule_config = proto_config.add_rules();
  auto* rule = rule_config->mutable_rule();
  auto* selector = rule->add_selectors();
  selector->set_key("usage");

  // Set on_present (required to pass the "at least one action" validation)
  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->set_type(
      envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::NUMBER);

  // Set on_error with explicit value field but leave the Value empty (kind_case == 0)
  auto* on_error = rule->mutable_on_error();
  on_error->set_metadata_namespace("envoy.lb");
  on_error->set_key("error");
  on_error->mutable_value(); // This sets the value oneof but leaves Value empty

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createParserFactory(proto_config, context), EnvoyException,
                            "on_error KeyValuePair with explicit value must have value set");
}

TEST(JsonContentParserConfigTest, RequiresAtLeastOneAction) {
  // Test that at least one of on_present, on_missing, or on_error must be specified
  const std::string yaml = R"EOF(
  rules:
    - rule:
        selectors:
          - key: "usage"
  )EOF";

  envoy::extensions::content_parsers::json::v3::JsonContentParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  JsonContentParserConfigFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createParserFactory(proto_config, context), EnvoyException,
      "At least one of on_present, on_missing, or on_error must be specified");
}

} // namespace
} // namespace Json
} // namespace ContentParsers
} // namespace Extensions
} // namespace Envoy
