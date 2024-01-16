#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "source/common/formatter/substitution_format_string.h"

#include "test/common/formatter/command_extension.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Formatter {

class SubstitutionFormatStringUtilsTest : public ::testing::Test {
public:
  SubstitutionFormatStringUtilsTest() {
    absl::optional<uint32_t> response_code{200};
    EXPECT_CALL(stream_info_, responseCode()).WillRepeatedly(Return(response_code));
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/bar/foo"}, {"content-type", "application/json"}};
  StreamInfo::MockStreamInfo stream_info_;

  HttpFormatterContext formatter_context_{&request_headers_};

  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(SubstitutionFormatStringUtilsTest, TestEmptyIsInvalid) {
  envoy::config::core::v3::SubstitutionFormatString empty_config;
  std::string err;
  EXPECT_FALSE(Validate(empty_config, &err));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigText) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "plain text, path=%REQ(:path)%, code=%RESPONSE_CODE%"
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("plain text, path=/bar/foo, code=200",
            formatter->formatWithContext(formatter_context_, stream_info_));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJson) {
  const std::string yaml = R"EOF(
  json_format:
    text: "plain text"
    path: "%REQ(:path)%"
    code: "%RESPONSE_CODE%"
    headers:
      content-type: "%REQ(CONTENT-TYPE)%"
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const auto out_json = formatter->formatWithContext(formatter_context_, stream_info_);

  const std::string expected = R"EOF({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200,
    "headers": {
      "content-type": "application/json"
    }
})EOF";
  EXPECT_TRUE(TestUtility::jsonStringEqual(out_json, expected));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestInvalidConfigs) {
  const std::vector<std::string> invalid_configs = {
      R"(
  json_format:
    field: true
)",
  };
  for (const auto& yaml : invalid_configs) {
    TestUtility::loadFromYaml(yaml, config_);
    EXPECT_THROW_WITH_MESSAGE(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_),
                              EnvoyException,
                              "Only string values, nested structs, list values and number values "
                              "are supported in structured access log format.");
  }
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigFormatterExtension) {
  TestCommandFactory factory;
  Registry::InjectFactory<CommandParserFactory> command_register(factory);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "plain text %COMMAND_EXTENSION()%"
  formatters:
    - name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("plain text TestFormatter",
            formatter->formatWithContext(formatter_context_, stream_info_));
}

TEST_F(SubstitutionFormatStringUtilsTest,
       TestFromProtoConfigFormatterExtensionFailsToCreateParser) {
  FailCommandFactory fail_factory;
  Registry::InjectFactory<CommandParserFactory> command_register(fail_factory);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "plain text"
  formatters:
    - name: envoy.formatter.FailFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.UInt64Value
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_MESSAGE(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_),
                            EnvoyException,
                            "Failed to create command parser: envoy.formatter.FailFormatter");
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigFormatterExtensionUnknown) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "plain text"
  formatters:
    - name: envoy.formatter.TestFormatterUnknown
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Any
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_MESSAGE(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_),
                            EnvoyException,
                            "Formatter not found: envoy.formatter.TestFormatterUnknown");
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonWithExtension) {
  TestCommandFactory factory;
  Registry::InjectFactory<CommandParserFactory> command_register(factory);

  const std::string yaml = R"EOF(
  json_format:
    text: "plain text %COMMAND_EXTENSION()%"
    path: "%REQ(:path)% %COMMAND_EXTENSION()%"
    code: "%RESPONSE_CODE% %COMMAND_EXTENSION()%"
    headers:
      content-type: "%REQ(CONTENT-TYPE)% %COMMAND_EXTENSION()%"
  formatters:
    - name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const auto out_json = formatter->formatWithContext(formatter_context_, stream_info_);

  const std::string expected = R"EOF({
    "text": "plain text TestFormatter",
    "path": "/bar/foo TestFormatter",
    "code": "200 TestFormatter",
    "headers": {
      "content-type": "application/json TestFormatter"
    }
})EOF";

  EXPECT_TRUE(TestUtility::jsonStringEqual(out_json, expected));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonWithMultipleExtensions) {
  TestCommandFactory test_factory;
  Registry::InjectFactory<CommandParserFactory> test_command_register(test_factory);
  AdditionalCommandFactory additional_factory;
  Registry::InjectFactory<CommandParserFactory> additional_command_register(additional_factory);

  const std::string yaml = R"EOF(
  json_format:
    text: "plain text %COMMAND_EXTENSION()%"
    path: "%REQ(:path)% %ADDITIONAL_EXTENSION()%"
  formatters:
    - name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
    - name: envoy.formatter.AdditionalFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.UInt32Value
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const auto out_json = formatter->formatWithContext(formatter_context_, stream_info_);

  const std::string expected = R"EOF({
    "text": "plain text TestFormatter",
    "path": "/bar/foo AdditionalFormatter",
})EOF";

  EXPECT_TRUE(TestUtility::jsonStringEqual(out_json, expected));
}

} // namespace Formatter
} // namespace Envoy
