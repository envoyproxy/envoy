#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"

#include "source/extensions/filters/http/aws_eventstream_to_metadata/config.h"
#include "source/extensions/filters/http/aws_eventstream_to_metadata/filter.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamToMetadata {
namespace {

TEST(AwsEventstreamToMetadataConfigTest, ValidConfig) {
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

  envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::AwsEventstreamToMetadata
      proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb_or.value()(filter_callback);
}

TEST(AwsEventstreamToMetadataConfigTest, MultipleRules) {
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

  envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::AwsEventstreamToMetadata
      proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

TEST(AwsEventstreamToMetadataConfigTest, EmptyConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamToMetadataConfig factory;

  envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::AwsEventstreamToMetadata
      empty_proto_config =
          *dynamic_cast<envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::
                            AwsEventstreamToMetadata*>(factory.createEmptyConfigProto().get());

  // Empty config should fail validation (no response_rules - required field)
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(empty_proto_config, "stats", context).IgnoreError(),
      EnvoyException, "Proto constraint validation failed.*value is required");
}

TEST(AwsEventstreamToMetadataConfigTest, CustomMaxBufferSize) {
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
    max_buffer_size: 2097152
  )EOF";

  envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::AwsEventstreamToMetadata
      proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  AwsEventstreamToMetadataConfig factory;
  auto cb_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(cb_or.ok());
}

} // namespace
} // namespace AwsEventstreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
