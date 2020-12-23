#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "common/formatter/substitution_format_string.h"

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
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  StreamInfo::MockStreamInfo stream_info_;
  std::string body_;

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

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api());
  EXPECT_EQ("plain text, path=/bar/foo, code=200",
            formatter->format(request_headers_, response_headers_, response_trailers_, stream_info_,
                              body_));
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

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api());
  const auto out_json = formatter->format(request_headers_, response_headers_, response_trailers_,
                                          stream_info_, body_);

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
      R"(
  json_format:
    field: 200
)",
  };
  for (const auto& yaml : invalid_configs) {
    TestUtility::loadFromYaml(yaml, config_);
    EXPECT_THROW_WITH_MESSAGE(
        SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api()), EnvoyException,
        "Only string values or nested structs are supported in structured access log format.");
  }
}

class TestFormatter : public FormatterProvider {
public:
  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override {
    return "TestFormatter";
  }
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override {
    return ValueUtil::stringValue("");
  }
};

class TestCommandParser : public CommandParser {
public:
  TestCommandParser() = default;

  FormatterProviderPtr parse(absl::string_view token) const override {
    if (absl::StartsWith(token, "COMMAND_EXTENSION")) {
      return std::make_unique<TestFormatter>();
    }

    return nullptr;
  }
};

class TestCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr createCommandParserFromProto(const Protobuf::Message&) override {
    return std::make_unique<TestCommandParser>();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
  std::string name() const override { return "envoy.formatter.TestFormatter"; }
};

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

  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api());
  EXPECT_EQ("plain text TestFormatter", formatter->format(request_headers_, response_headers_,
                                                          response_trailers_, stream_info_, body_));
}

TEST_F(SubstitutionFormatStringUtilsTest, TestFromProtoConfigFormatterExtensionUnknown) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "plain text"
  formatters:
    - name: envoy.formatter.TestFormatterUnknown
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_MESSAGE(SubstitutionFormatStringUtils::fromProtoConfig(config_, context_.api()),
                            EnvoyException,
                            "Formatter not found: envoy.formatter.TestFormatterUnknown");
}

} // namespace Formatter
} // namespace Envoy
