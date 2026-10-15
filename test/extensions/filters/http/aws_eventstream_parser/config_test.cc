#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"

#include "source/extensions/filters/http/aws_eventstream_parser/config.h"
#include "source/extensions/filters/http/aws_eventstream_parser/filter.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamParser {
namespace {

TEST(AwsEventstreamParserConfigTest, ValidConfig) {
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

  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamParserConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb_or.value()(filter_callback);
}

TEST(AwsEventstreamParserConfigTest, MultipleRules) {
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

  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamParserConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

TEST(AwsEventstreamParserConfigTest, EmptyConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamParserConfig factory;

  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser
      empty_proto_config = *dynamic_cast<
          envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser*>(
          factory.createEmptyConfigProto().get());

  // Empty config should fail validation (no response_rules - required field)
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(empty_proto_config, "stats", context).IgnoreError(),
      EnvoyException, "Proto constraint validation failed.*value is required");
}

TEST(AwsEventstreamParserConfigTest, ValidHeaderRulesConfig) {
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
                key: "tokens"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
      - header_name: ":message-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "message_type"
        on_missing:
          metadata_namespace: "envoy.lb"
          key: "message_type"
          value:
            string_value: "unknown"
  )EOF";

  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamParserConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

TEST(AwsEventstreamParserConfigTest, HeaderRuleEmptyHeaderName) {
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
                key: "tokens"
    header_rules:
      - header_name: ""
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
  )EOF";

  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException,
                          "Proto constraint validation failed");
}

TEST(AwsEventstreamParserConfigTest, HeaderRuleEmptyKey) {
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
                key: "tokens"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: ""
  )EOF";

  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException,
                          "Proto constraint validation failed");
}

TEST(AwsEventstreamParserConfigTest, HeaderRuleStopProcessingAfterMatchesExceedsMax) {
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
                key: "tokens"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
        stop_processing_after_matches: 2
  )EOF";

  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException,
                          "Proto constraint validation failed");
}

} // namespace
} // namespace AwsEventstreamParser
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
