#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class ReqWithoutQueryTest : public ::testing::Test {
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
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/request/path", formatter->formatWithContext(formatter_context_, stream_info_));
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
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/original/path", formatter->formatWithContext(formatter_context_, stream_info_));
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
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/request/path", formatter->formatWithContext(formatter_context_, stream_info_));
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
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("/requ", formatter->formatWithContext(formatter_context_, stream_info_));
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
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("-", formatter->formatWithContext(formatter_context_, stream_info_));
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
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->formatWithContext(formatter_context_, stream_info_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(actual, expected));
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

  EXPECT_THROW(Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_),
               EnvoyException);
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
