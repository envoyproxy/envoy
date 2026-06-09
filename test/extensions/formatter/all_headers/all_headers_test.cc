#include "source/common/formatter/substitution_format_string.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace {

const std::string AllHeadersFormatterConfig = R"EOF(
  formatters:
    - name: envoy.formatter.all_headers
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.all_headers.v3.AllHeaders
)EOF";

const std::string AllHeadersFormatterConfigWithExclude = R"EOF(
  formatters:
    - name: envoy.formatter.all_headers
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.all_headers.v3.AllHeaders
        exclude_headers:
          - "authorization"
          - "cookie"
          - "set-cookie"
)EOF";

const std::string AllHeadersFormatterConfigWithMaxBytes = R"EOF(
  formatters:
    - name: envoy.formatter.all_headers
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.all_headers.v3.AllHeaders
        max_value_bytes: 10
)EOF";

const std::string AllHeadersFormatterConfigWithExcludeAndMaxBytes = R"EOF(
  formatters:
    - name: envoy.formatter.all_headers
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.all_headers.v3.AllHeaders
        max_value_bytes: 10
        exclude_headers:
          - "authorization"
)EOF";

class AllHeadersFormatterTest : public ::testing::Test {
public:
  AllHeadersFormatterTest() {
    formatter_context_.setRequestHeaders(request_headers_);
    formatter_context_.setResponseHeaders(response_headers_);
  }

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"},
                                                  {":path", "/api/v1/resource"},
                                                  {":authority", "example.com"},
                                                  {"x-request-id", "abc-123"},
                                                  {"user-agent", "test-client/1.0"}};

  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"content-type", "application/json"},
                                                    {"x-custom-header", "custom-value"}};

  StreamInfo::MockStreamInfo stream_info_;
  Envoy::Formatter::Context formatter_context_;
  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

// REQ_ALL_HEADERS returns a JSON object with all request headers.
TEST_F(AllHeadersFormatterTest, TestAllRequestHeaders) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(formatter_context_, stream_info_);

  // Result should be a JSON string containing all request headers.
  EXPECT_THAT(result, testing::HasSubstr("\"x-request-id\":\"abc-123\""));
  EXPECT_THAT(result, testing::HasSubstr("\"user-agent\":\"test-client/1.0\""));
  EXPECT_THAT(result, testing::HasSubstr("\":method\":\"GET\""));
  EXPECT_THAT(result, testing::HasSubstr("\":path\":\"/api/v1/resource\""));
  EXPECT_THAT(result, testing::HasSubstr("\":authority\":\"example.com\""));
}

// RESP_ALL_HEADERS returns a JSON object with all response headers.
TEST_F(AllHeadersFormatterTest, TestAllResponseHeaders) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%RESP_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(formatter_context_, stream_info_);

  EXPECT_THAT(result, testing::HasSubstr("\":status\":\"200\""));
  EXPECT_THAT(result, testing::HasSubstr("\"content-type\":\"application/json\""));
  EXPECT_THAT(result, testing::HasSubstr("\"x-custom-header\":\"custom-value\""));
}

// When no request headers are available, text format returns "-".
TEST_F(AllHeadersFormatterTest, TestNoRequestHeaders) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  Envoy::Formatter::Context empty_context;
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("-", formatter->format(empty_context, stream_info_));
}

// When no response headers are available, text format returns "-".
TEST_F(AllHeadersFormatterTest, TestNoResponseHeaders) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%RESP_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  Envoy::Formatter::Context empty_context;
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("-", formatter->format(empty_context, stream_info_));
}

// Empty header map produces an empty JSON object.
TEST_F(AllHeadersFormatterTest, TestEmptyRequestHeaders) {
  Http::TestRequestHeaderMapImpl empty_headers;
  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(empty_headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  EXPECT_EQ("{}", formatter->format(ctx, stream_info_));
}

// Pseudo-headers (:method, :path, :authority) are included in the output.
TEST_F(AllHeadersFormatterTest, TestPseudoHeadersIncluded) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(formatter_context_, stream_info_);

  EXPECT_THAT(result, testing::HasSubstr("\":method\""));
  EXPECT_THAT(result, testing::HasSubstr("\":path\""));
  EXPECT_THAT(result, testing::HasSubstr("\":authority\""));
}

// Repeated headers with the same name are comma-joined per RFC 7230.
TEST_F(AllHeadersFormatterTest, TestMultiValueHeadersCommaJoined) {
  Http::TestRequestHeaderMapImpl multi_headers{{":method", "GET"}, {":path", "/"}};
  multi_headers.addCopy(Http::LowerCaseString("x-forwarded-for"), "10.0.0.1");
  multi_headers.addCopy(Http::LowerCaseString("x-forwarded-for"), "10.0.0.2");
  multi_headers.addCopy(Http::LowerCaseString("x-forwarded-for"), "10.0.0.3");

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(multi_headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(ctx, stream_info_);

  EXPECT_THAT(result, testing::HasSubstr("\"x-forwarded-for\":\"10.0.0.1,10.0.0.2,10.0.0.3\""));
}

// max_value_bytes truncates individual header values that exceed the limit.
TEST_F(AllHeadersFormatterTest, TestMaxValueBytesTruncation) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {"x-long-header", "this-value-is-longer-than-ten"}};

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfigWithMaxBytes;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(ctx, stream_info_);

  EXPECT_THAT(result, testing::HasSubstr("\"x-long-header\":\"this-value\""));
  EXPECT_THAT(result, testing::Not(testing::HasSubstr("this-value-is-longer-than-ten")));
}

// Values shorter than max_value_bytes are not truncated.
TEST_F(AllHeadersFormatterTest, TestMaxValueBytesNoTruncationForShortValues) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {"x-short", "hi"}};

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfigWithMaxBytes;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(ctx, stream_info_);

  EXPECT_THAT(result, testing::HasSubstr("\"x-short\":\"hi\""));
}

// exclude_headers removes specified headers from the output.
TEST_F(AllHeadersFormatterTest, TestExcludeHeaders) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/"},
                                         {"authorization", "Bearer secret-token"},
                                         {"cookie", "session=abc123"},
                                         {"x-request-id", "req-456"}};

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfigWithExclude;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(ctx, stream_info_);

  EXPECT_THAT(result, testing::Not(testing::HasSubstr("authorization")));
  EXPECT_THAT(result, testing::Not(testing::HasSubstr("cookie")));
  EXPECT_THAT(result, testing::Not(testing::HasSubstr("secret-token")));
  EXPECT_THAT(result, testing::Not(testing::HasSubstr("session=abc123")));
  EXPECT_THAT(result, testing::HasSubstr("\"x-request-id\":\"req-456\""));
}

// exclude_headers matching is case-insensitive.
TEST_F(AllHeadersFormatterTest, TestExcludeHeadersCaseInsensitive) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {"Authorization", "Bearer token"}};

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfigWithExclude;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(ctx, stream_info_);

  EXPECT_THAT(result, testing::Not(testing::HasSubstr("\"authorization\":")));
  EXPECT_THAT(result, testing::Not(testing::HasSubstr("Bearer token")));
}

// exclude_headers on response headers works the same way.
TEST_F(AllHeadersFormatterTest, TestExcludeResponseHeaders) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"set-cookie", "session=xyz"}, {"content-type", "text/html"}};

  Envoy::Formatter::Context ctx;
  ctx.setResponseHeaders(headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%RESP_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfigWithExclude;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(ctx, stream_info_);

  EXPECT_THAT(result, testing::Not(testing::HasSubstr("set-cookie")));
  EXPECT_THAT(result, testing::HasSubstr("\"content-type\":\"text/html\""));
}

// Both max_value_bytes and exclude_headers work together.
TEST_F(AllHeadersFormatterTest, TestExcludeAndMaxBytesCombined) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/"},
                                         {"authorization", "Bearer secret-token"},
                                         {"x-long", "abcdefghijklmnop"}};

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfigWithExcludeAndMaxBytes;
  TestUtility::loadFromYaml(yaml, config_);

  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string result = formatter->format(ctx, stream_info_);

  EXPECT_THAT(result, testing::Not(testing::HasSubstr("authorization")));
  EXPECT_THAT(result, testing::HasSubstr("\"x-long\":\"abcdefghij\""));
  EXPECT_THAT(result, testing::Not(testing::HasSubstr("abcdefghijklmnop")));
}

// In JSON format, REQ_ALL_HEADERS produces a nested JSON object (not a flat string).
TEST_F(AllHeadersFormatterTest, TestJsonFormatRequestHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/test"}, {"x-request-id", "id-789"}};

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  json_format:
    request_headers: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  const std::string expected = R"EOF({
    "request_headers": {
      ":method": "GET",
      ":path": "/test",
      "x-request-id": "id-789"
    }
  })EOF";

  TestUtility::loadFromYaml(yaml, config_);
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->format(ctx, stream_info_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(actual, expected));
}

// In JSON format, RESP_ALL_HEADERS produces a nested JSON object.
TEST_F(AllHeadersFormatterTest, TestJsonFormatResponseHeaders) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "404"}, {"content-type", "text/plain"}, {"x-error-code", "NOT_FOUND"}};

  Envoy::Formatter::Context ctx;
  ctx.setResponseHeaders(headers);

  const std::string yaml = R"EOF(
  json_format:
    response_headers: "%RESP_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  const std::string expected = R"EOF({
    "response_headers": {
      ":status": "404",
      "content-type": "text/plain",
      "x-error-code": "NOT_FOUND"
    }
  })EOF";

  TestUtility::loadFromYaml(yaml, config_);
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->format(ctx, stream_info_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(actual, expected));
}

// In JSON format, missing headers produce null (not "-").
TEST_F(AllHeadersFormatterTest, TestJsonFormatMissingHeaders) {
  const std::string yaml = R"EOF(
  json_format:
    request_headers: "%REQ_ALL_HEADERS%"
    response_headers: "%RESP_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  const std::string expected = R"EOF({
    "request_headers": null,
    "response_headers": null
  })EOF";

  TestUtility::loadFromYaml(yaml, config_);
  Envoy::Formatter::Context empty_context;
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->format(empty_context, stream_info_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(actual, expected));
}

// In JSON format, both request and response headers appear as separate nested objects.
TEST_F(AllHeadersFormatterTest, TestJsonFormatBothHeaderTypes) {
  const std::string yaml = R"EOF(
  json_format:
    request_headers: "%REQ_ALL_HEADERS%"
    response_headers: "%RESP_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;

  TestUtility::loadFromYaml(yaml, config_);
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->format(formatter_context_, stream_info_);

  // Verify both fields are present as objects (parse succeeds and contains expected keys).
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(actual).value();
  EXPECT_TRUE(json->getObject("request_headers").ok());
  EXPECT_TRUE(json->getObject("response_headers").ok());
}

// In JSON format, multi-value headers are comma-joined in the nested object.
TEST_F(AllHeadersFormatterTest, TestJsonFormatMultiValueHeaders) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/submit"}};
  headers.addCopy(Http::LowerCaseString("accept"), "text/html");
  headers.addCopy(Http::LowerCaseString("accept"), "application/json");

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  json_format:
    request_headers: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfig;
  const std::string expected = R"EOF({
    "request_headers": {
      ":method": "POST",
      ":path": "/submit",
      "accept": "text/html,application/json"
    }
  })EOF";

  TestUtility::loadFromYaml(yaml, config_);
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->format(ctx, stream_info_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(actual, expected));
}

// In JSON format with exclude_headers, excluded headers don't appear in the nested object.
TEST_F(AllHeadersFormatterTest, TestJsonFormatWithExclude) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {"authorization", "Bearer token"}, {"x-safe", "ok"}};

  Envoy::Formatter::Context ctx;
  ctx.setRequestHeaders(headers);

  const std::string yaml = R"EOF(
  json_format:
    request_headers: "%REQ_ALL_HEADERS%"
)EOF" + AllHeadersFormatterConfigWithExclude;
  const std::string expected = R"EOF({
    "request_headers": {
      ":method": "GET",
      ":path": "/",
      "x-safe": "ok"
    }
  })EOF";

  TestUtility::loadFromYaml(yaml, config_);
  auto formatter =
      *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  const std::string actual = formatter->format(ctx, stream_info_);
  EXPECT_TRUE(TestUtility::jsonStringEqual(actual, expected));
}

// The parser does not recognize unknown commands and the config fails to load.
TEST_F(AllHeadersFormatterTest, TestParserNotRecognizingCommand) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%COMMAND_THAT_DOES_NOT_EXIST()%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_FALSE(Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_)
                   .status()
                   .ok());
}

// Subcommands are rejected (e.g. %REQ_ALL_HEADERS(foo)% is invalid).
TEST_F(AllHeadersFormatterTest, TestRejectsSubcommand) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS(some-header)%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_REGEX(
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_)
          .IgnoreError(),
      EnvoyException, "REQ_ALL_HEADERS does not accept subcommands");
}

// Max length parameter is rejected (e.g. %REQ_ALL_HEADERS():10% is invalid).
TEST_F(AllHeadersFormatterTest, TestRejectsMaxLength) {
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%REQ_ALL_HEADERS():10%"
)EOF" + AllHeadersFormatterConfig;
  TestUtility::loadFromYaml(yaml, config_);

  EXPECT_THROW_WITH_REGEX(
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_)
          .IgnoreError(),
      EnvoyException, "REQ_ALL_HEADERS does not accept a max_length parameter");
}

} // namespace
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
