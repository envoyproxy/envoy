#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/formatter/cel/cel.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class CELFormatterTest : public ::testing::Test {
public:
  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"},
      {":path", "/request/path?secret=parameter"},
      {"x-envoy-original-path", "/original/path?secret=parameter"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  StreamInfo::MockStreamInfo stream_info_;
  std::string body_;

  Envoy::Formatter::HttpFormatterContext formatter_context_{&request_headers_, &response_headers_,
                                                            &response_trailers_, body_};

  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

#ifdef USE_CEL_PARSER
TEST_F(CELFormatterTest, TestFormatValue) {
  auto cel_parser = std::make_unique<CELFormatterCommandParser>(context_);
  absl::optional<size_t> max_length = absl::nullopt;
  auto formatter = cel_parser->parse("CEL", "request.headers[':method']", max_length);
  EXPECT_THAT(formatter->formatValueWithContext(formatter_context_, stream_info_),
              ProtoEq(ValueUtil::stringValue("GET")));
}

TEST_F(CELFormatterTest, TestParseFail) {
  auto cel_parser = std::make_unique<CELFormatterCommandParser>(context_);
  absl::optional<size_t> max_length = absl::nullopt;
  EXPECT_EQ(nullptr,
            cel_parser->parse("INVALID_CMD", "requests.headers['missing_headers']", max_length));
}

TEST_F(CELFormatterTest, TestNullFormatValue) {
  auto cel_parser = std::make_unique<CELFormatterCommandParser>(context_);
  absl::optional<size_t> max_length = absl::nullopt;
  auto formatter = cel_parser->parse("CEL", "requests.headers['missing_headers']", max_length);
  EXPECT_THAT(formatter->formatValueWithContext(formatter_context_, stream_info_),
              ProtoEq(ValueUtil::nullValue()));
}

TEST_F(CELFormatterTest, TestRequestHeader) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%CEL(request.headers[':method'])%"
  formatters:
    - name: envoy.formatter.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("GET", formatter->formatWithContext(formatter_context_, stream_info_));
}

TEST_F(CELFormatterTest, TestMissingRequestHeader) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%CEL(request.headers['missing-header'])%"
  formatters:
    - name: envoy.formatter.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("-", formatter->formatWithContext(formatter_context_, stream_info_));
}

TEST_F(CELFormatterTest, TestWithoutMaxLength) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%CEL(request.headers['x-envoy-original-path'])%"
  formatters:
    - name: envoy.formatter.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/original/path?secret=parameter",
            formatter->formatWithContext(formatter_context_, stream_info_));
}

TEST_F(CELFormatterTest, TestMaxLength) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%CEL(request.headers['x-envoy-original-path']):9%"
  formatters:
    - name: envoy.formatter.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/original", formatter->formatWithContext(formatter_context_, stream_info_));
}

TEST_F(CELFormatterTest, TestInvalidExpression) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%CEL(+++++)%"
  formatters:
    - name: envoy.formatter.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_REGEX(
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_),
      EnvoyException, "Not able to parse filter expression: .*");
}
#endif

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
