#include "source/common/formatter/substitution_format_string.h"
#include "source/extensions/formatter/req_without_query/req_without_query.h"

#include "test/common/formatter/formatter_test_utility.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;

class ReqWithoutQueryTest : public ::testing::Test {
public:
  ReqWithoutQueryTest() { formatter_context_.setRequestHeaders(request_headers_); }
  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"},
      {":path", "/request/path?secret=parameter"},
      {"x-envoy-original-path", "/original/path?secret=parameter"}};

  StreamInfo::MockStreamInfo stream_info_;

  Envoy::Formatter::Context formatter_context_;

  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(ReqWithoutQueryTest, TestStripQueryString) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(:PATH)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/request/path", formatter->format(formatter_context_, stream_info_));
}

TEST_F(ReqWithoutQueryTest, TestEmptyHeader) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(:PATH)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  Envoy::Formatter::Context formatter_context;
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("-", formatter->format(formatter_context, stream_info_));
}

TEST_F(ReqWithoutQueryTest, TestSelectMainHeader) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(X-ENVOY-ORIGINAL-PATH?:PATH)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/original/path", formatter->format(formatter_context_, stream_info_));
}

TEST_F(ReqWithoutQueryTest, TestSelectAlternativeHeader) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(X-NON-EXISTING-HEADER?:PATH)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/request/path", formatter->format(formatter_context_, stream_info_));
}

TEST_F(ReqWithoutQueryTest, TestTruncateHeader) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(:PATH):5%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/requ", formatter->format(formatter_context_, stream_info_));
}

TEST_F(ReqWithoutQueryTest, TestNonExistingHeader) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_WITHOUT_QUERY(does-not-exist)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("-", formatter->format(formatter_context_, stream_info_));
}

TEST_F(ReqWithoutQueryTest, TestFormatJson) {
  const std::string yaml = R"EOF(
  json_format:
    no_query: "%REQ_WITHOUT_QUERY(:PATH)%"
    select_main_header: "%REQ_WITHOUT_QUERY(X-ENVOY-ORIGINAL-PATH?:PATH)%"
    select_alt_header: "%REQ_WITHOUT_QUERY(X-NON-EXISTING-HEADER?:PATH)%"
    truncate: "%REQ_WITHOUT_QUERY(:PATH):5%"
    does_not_exist: "%REQ_WITHOUT_QUERY(does-not-exist)%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  const std::string expected = R"EOF({
    "no_query": "/request/path",
    "select_main_header": "/original/path",
    "select_alt_header": "/request/path",
    "truncate": "/requ",
    "does_not_exist": null
})EOF";

  TestUtility::loadFromYaml(yaml, config_);
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->format(formatter_context_, stream_info_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(actual, expected));
}

// Drives the provider directly so that format()/formatValue() and their sink-based counterparts
// are checked against each other. The line formatters above only exercise the sink-based paths.
TEST_F(ReqWithoutQueryTest, TestProviderFormatMatchesFormatTo) {
  ReqWithoutQueryCommandParser parser;

  // The main header is used and the query string is stripped.
  {
    auto provider = *parser.parse("REQ_WITHOUT_QUERY", ":PATH", std::nullopt);
    ASSERT_NE(nullptr, provider);
    EXPECT_EQ("/request/path",
              Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_));
    EXPECT_EQ("/request/path",
              Envoy::Formatter::formatValueForTest(*provider, formatter_context_, stream_info_)
                  .string_value());
  }

  // The alternative header is used when the main one is absent.
  {
    auto provider = *parser.parse("REQ_WITHOUT_QUERY", "X-NON-EXISTING-HEADER?:PATH", std::nullopt);
    ASSERT_NE(nullptr, provider);
    EXPECT_EQ("/request/path",
              Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_));
    EXPECT_EQ("/request/path",
              Envoy::Formatter::formatValueForTest(*provider, formatter_context_, stream_info_)
                  .string_value());
  }

  // The value is truncated to the configured max length.
  {
    auto provider = *parser.parse("REQ_WITHOUT_QUERY", ":PATH", 5);
    ASSERT_NE(nullptr, provider);
    EXPECT_EQ("/requ",
              Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_));
    EXPECT_EQ("/requ",
              Envoy::Formatter::formatValueForTest(*provider, formatter_context_, stream_info_)
                  .string_value());
  }

  // A missing header is reported as no value at all.
  {
    auto provider = *parser.parse("REQ_WITHOUT_QUERY", "does-not-exist", std::nullopt);
    ASSERT_NE(nullptr, provider);
    EXPECT_EQ(std::nullopt,
              Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_));
    EXPECT_TRUE(Envoy::Formatter::formatValueForTest(*provider, formatter_context_, stream_info_)
                    .has_null_value());
  }

  // No request headers at all is also reported as no value.
  {
    auto provider = *parser.parse("REQ_WITHOUT_QUERY", ":PATH", std::nullopt);
    ASSERT_NE(nullptr, provider);
    Envoy::Formatter::Context empty_context;
    EXPECT_EQ(std::nullopt,
              Envoy::Formatter::formatForTest(*provider, empty_context, stream_info_));
    EXPECT_TRUE(Envoy::Formatter::formatValueForTest(*provider, empty_context, stream_info_)
                    .has_null_value());
  }

  // A command the parser does not own yields no provider.
  EXPECT_EQ(nullptr, *parser.parse("NOT_REQ_WITHOUT_QUERY", "", std::nullopt));
}

TEST_F(ReqWithoutQueryTest, TestParserNotRecognizingCommand) {

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%COMMAND_THAT_DOES_NOT_EXIST()%"
  formatters:
    - name: envoy.formatter.req_without_query
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery
)EOF";
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THAT(
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_).status(),
      Not(IsOk()));
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
