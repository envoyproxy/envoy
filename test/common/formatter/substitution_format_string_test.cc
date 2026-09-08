#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "source/common/formatter/substitution_format_string.h"

#include "test/common/formatter/command_extension.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Formatter {

using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::testing::NotNull;
using ::testing::Return;

class SubstitutionFormatStringUtilsTest : public ::testing::Test {
public:
  SubstitutionFormatStringUtilsTest() {
    std::optional<uint32_t> response_code{200};
    EXPECT_CALL(stream_info_, responseCode()).WillRepeatedly(Return(response_code));

    formatter_context_.setRequestHeaders(request_headers_);
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/bar/foo"}, {"content-type", "application/json"}};
  StreamInfo::MockStreamInfo stream_info_;

  Context formatter_context_;

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

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("plain text, path=/bar/foo, code=200",
            formatter->format(formatter_context_, stream_info_));
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

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const auto out_json = formatter->format(formatter_context_, stream_info_);

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

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValues) {
  const std::string yaml = R"EOF(
  json_format:
    present_string: "plain"
    present_req: "%REQ(:path)%"
    present_code: "%RESPONSE_CODE%"
    missing_req: "%REQ(missing-header)%"
    number_value: 42
    bool_value: true
    multi_token: "%REQ(missing-header)%-%REQ(:method)%"
    both_missing: "%REQ(missing-x)%%REQ(missing-y)%"
    empty_nested:
      a: "%REQ(missing-a)%"
      b: "%REQ(missing-b)%"
    partial_nested:
      present: "%REQ(:method)%"
      missing: "%REQ(missing-c)%"
    deep:
      deeper:
        deepest: "%REQ(missing-d)%"
    array_value:
      - "%REQ(:method)%"
      - "%REQ(missing-e)%"
      - "plain_in_array"
    empty_array:
      - "%REQ(missing-f)%"
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const auto out_json = formatter->format(formatter_context_, stream_info_);

  // Keys with null values are omitted, nested objects that become empty are removed, empty arrays
  // are preserved and values that resolve are kept with their type (the response code stays a
  // number).
  const std::string expected = R"EOF({
    "present_string": "plain",
    "present_req": "/bar/foo",
    "present_code": 200,
    "number_value": 42,
    "bool_value": true,
    "multi_token": "-GET",
    "both_missing": "",
    "partial_nested": {
      "present": "GET"
    },
    "array_value": [
      "GET",
      "plain_in_array"
    ],
    "empty_array": []
})EOF";
  EXPECT_TRUE(TestUtility::jsonStringEqual(out_json, expected));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesRootEmpty) {
  const std::string yaml = R"EOF(
  json_format:
    missing_a: "%REQ(missing-a)%"
    missing_b: "%REQ(missing-b)%"
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  // The root object is always emitted even when every field is omitted.
  EXPECT_TRUE(
      TestUtility::jsonStringEqual(formatter->format(formatter_context_, stream_info_), "{}"));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesNullLiteral) {
  config_.set_omit_empty_values(true);
  auto& fields = *config_.mutable_json_format()->mutable_fields();
  fields["present"].set_string_value("plain");
  fields["explicit_null"].set_null_value(Protobuf::NULL_VALUE);
  // A default-constructed value has no kind set and must be dropped like an explicit null.
  fields["unset"] = Protobuf::Value();

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  // A literal null or an unset value in the configuration is treated as empty and omitted.
  EXPECT_TRUE(TestUtility::jsonStringEqual(formatter->format(formatter_context_, stream_info_),
                                           R"({"present":"plain"})"));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesListElements) {
  config_.set_omit_empty_values(true);
  auto& fields = *config_.mutable_json_format()->mutable_fields();
  auto* elements = fields["arr"].mutable_list_value();
  elements->add_values()->set_string_value("%REQ(:method)%");   // Present -> "GET".
  elements->add_values()->set_string_value("%REQ(missing)%");   // Missing -> skipped.
  elements->add_values()->set_null_value(Protobuf::NULL_VALUE); // Literal null -> skipped.
  // A nested object whose only field is null collapses and is dropped from the array.
  (*elements->add_values()->mutable_struct_value()->mutable_fields())["gone"].set_string_value(
      "%REQ(missing)%");
  // A nested object with a resolvable field is kept.
  (*elements->add_values()->mutable_struct_value()->mutable_fields())["present"].set_string_value(
      "%REQ(:method)%");

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  // Missing, null-literal and fully-empty-object elements are skipped; the array is kept.
  EXPECT_TRUE(TestUtility::jsonStringEqual(formatter->format(formatter_context_, stream_info_),
                                           R"({"arr":["GET",{"present":"GET"}]})"));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesEmptyContainers) {
  const std::string yaml = R"EOF(
  json_format:
    present: "plain"
    empty_object: {}
    empty_list: []
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  // A configuration-time empty object is dropped, but an empty array is preserved.
  EXPECT_TRUE(TestUtility::jsonStringEqual(formatter->format(formatter_context_, stream_info_),
                                           R"({"present":"plain","empty_list":[]})"));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesSanitization) {
  config_.set_omit_empty_values(true);
  auto& fields = *config_.mutable_json_format()->mutable_fields();
  // Keys and constant string values are JSON-sanitized.
  fields[R"(quote"key)"].set_string_value("%REQ(:method)%");
  fields["const"].set_string_value(R"(a"b)");

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(formatter->format(formatter_context_, stream_info_),
                                           R"({"const":"a\"b","quote\"key":"GET"})"));
}

TEST_F(SubstitutionFormatStringUtilsTest,
       TestFromProtoConfigJsonOmitEmptyValuesPresentEmptyString) {
  request_headers_.addCopy("x-empty", "");

  const std::string yaml = R"EOF(
  json_format:
    empty_present: "%REQ(x-empty)%"
    missing: "%REQ(missing-header)%"
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  // A present-but-empty header is kept as an empty string, while a truly missing one is omitted.
  EXPECT_TRUE(TestUtility::jsonStringEqual(formatter->format(formatter_context_, stream_info_),
                                           R"({"empty_present":""})"));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesInvalidFlat) {
  const std::string yaml = R"EOF(
  json_format:
    invalid: "%NOT_A_REAL_COMMAND%"
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);
  EXPECT_FALSE(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_).ok());
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesInvalidNested) {
  const std::string yaml = R"EOF(
  json_format:
    nested:
      bad: "%NOT_A_REAL_COMMAND%"
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);
  EXPECT_FALSE(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_).ok());
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigJsonOmitEmptyValuesInvalidList) {
  const std::string yaml = R"EOF(
  json_format:
    arr:
      - "%NOT_A_REAL_COMMAND%"
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);
  EXPECT_FALSE(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_).ok());
}

TEST_F(SubstitutionFormatStringUtilsTest,
       TestFromProtoConfigJsonOmitEmptyValuesDisabledByRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.json_formatter_omit_empty_values", "false"}});

  const std::string yaml = R"EOF(
  json_format:
    missing_req: "%REQ(missing-header)%"
  omit_empty_values: true
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  // With the runtime guard disabled the pre-serialized formatter is used, which keeps null keys.
  EXPECT_TRUE(TestUtility::jsonStringEqual(formatter->format(formatter_context_, stream_info_),
                                           R"({"missing_req":null})"));
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

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("plain text TestFormatter", formatter->format(formatter_context_, stream_info_));
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

  EXPECT_EQ(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_).status().message(),
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

  EXPECT_EQ(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_).status().message(),
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

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const auto out_json = formatter->format(formatter_context_, stream_info_);

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

  auto formatter = *SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const auto out_json = formatter->format(formatter_context_, stream_info_);

  const std::string expected = R"EOF({
    "text": "plain text TestFormatter",
    "path": "/bar/foo AdditionalFormatter",
})EOF";

  EXPECT_TRUE(TestUtility::jsonStringEqual(out_json, expected));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestParseFormattersWithUnknownExtension) {
  const std::string yaml = R"EOF(
      name: envoy.formatter.TestFormatterUnknown
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Any
  )EOF";

  SubstitutionFormatStringUtils::FormattersConfig config;
  auto* entry1 = config.Add();
  envoy::config::core::v3::TypedExtensionConfig proto;
  TestUtility::loadFromYaml(yaml, proto);
  *entry1 = proto;

  EXPECT_EQ(SubstitutionFormatStringUtils::parseFormatters(config, context_).status().message(),
            "Formatter not found: envoy.formatter.TestFormatterUnknown");
}

TEST_F(SubstitutionFormatStringUtilsTest, TestParseFormattersWithInvalidFormatter) {
  FailCommandFactory fail_factory;
  Registry::InjectFactory<CommandParserFactory> command_register(fail_factory);

  const std::string yaml = R"EOF(
      name: envoy.formatter.FailFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.UInt64Value
  )EOF";

  SubstitutionFormatStringUtils::FormattersConfig config;
  auto* entry1 = config.Add();
  envoy::config::core::v3::TypedExtensionConfig proto;
  TestUtility::loadFromYaml(yaml, proto);
  *entry1 = proto;

  EXPECT_EQ(SubstitutionFormatStringUtils::parseFormatters(config, context_).status().message(),
            "Failed to create command parser: envoy.formatter.FailFormatter");
}

TEST_F(SubstitutionFormatStringUtilsTest, TestParseFormattersWithSingleExtension) {
  TestCommandFactory factory;
  Registry::InjectFactory<CommandParserFactory> command_register(factory);

  const std::string yaml = R"EOF(
      name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  )EOF";

  SubstitutionFormatStringUtils::FormattersConfig config;
  auto* entry1 = config.Add();
  envoy::config::core::v3::TypedExtensionConfig proto;
  TestUtility::loadFromYaml(yaml, proto);
  *entry1 = proto;

  auto commands = *SubstitutionFormatStringUtils::parseFormatters(config, context_);
  ASSERT_EQ(1, commands.size());

  std::optional<size_t> max_length = {};
  ASSERT_TRUE(commands[0] != nullptr);
  auto status_or_provider = commands[0]->parse("COMMAND_EXTENSION", "", max_length);
  ASSERT_THAT(status_or_provider, IsOkAndHolds(NotNull()));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestParseFormattersWithMultipleExtensions) {
  TestCommandFactory factory;
  Registry::InjectFactory<CommandParserFactory> command_register(factory);
  AdditionalCommandFactory additional_factory;
  Registry::InjectFactory<CommandParserFactory> additional_command_register(additional_factory);

  const std::string test_command_yaml = R"EOF(
      name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  )EOF";

  const std::string additional_command_yaml = R"EOF(
      name: envoy.formatter.AdditionalFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.UInt32Value
  )EOF";

  SubstitutionFormatStringUtils::FormattersConfig config;

  auto* entry1 = config.Add();
  envoy::config::core::v3::TypedExtensionConfig test_command_proto;
  TestUtility::loadFromYaml(test_command_yaml, test_command_proto);
  *entry1 = test_command_proto;

  auto* entry2 = config.Add();
  envoy::config::core::v3::TypedExtensionConfig additional_command_proto;
  TestUtility::loadFromYaml(additional_command_yaml, additional_command_proto);
  *entry2 = additional_command_proto;

  auto commands = *SubstitutionFormatStringUtils::parseFormatters(config, context_);
  ASSERT_EQ(2, commands.size());

  std::optional<size_t> max_length = {};
  ASSERT_TRUE(commands[0] != nullptr);
  auto test_command_provider_or_status = commands[0]->parse("COMMAND_EXTENSION", "", max_length);
  ASSERT_THAT(test_command_provider_or_status, IsOkAndHolds(NotNull()));
  ASSERT_TRUE(commands[1] != nullptr);
  auto additional_command_provider_or_status =
      commands[1]->parse("ADDITIONAL_EXTENSION", "", max_length);
  ASSERT_THAT(additional_command_provider_or_status, IsOkAndHolds(NotNull()));
}

} // namespace Formatter
} // namespace Envoy
