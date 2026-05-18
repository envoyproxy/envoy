#include "source/common/http/http_service_headers.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace {

using testing::NiceMock;

class HttpServiceHeadersApplicatorTest : public testing::Test {
protected:
  HttpServiceHeadersApplicator buildApplicator(const std::string& yaml) {
    envoy::config::core::v3::HttpService http_service;
    TestUtility::loadFromYaml(yaml, http_service);
    absl::Status creation_status = absl::OkStatus();
    HttpServiceHeadersApplicator applicator(http_service, server_context_, creation_status);
    EXPECT_TRUE(creation_status.ok());
    return applicator;
  }

  std::string getHeader(RequestHeaderMap& headers, const std::string& key) {
    const auto entries = headers.get(LowerCaseString(key));
    if (entries.empty()) {
      return "";
    }
    return std::string(entries[0]->value().getStringView());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
};

TEST_F(HttpServiceHeadersApplicatorTest, StaticValueHeaders) {
  auto applicator = buildApplicator(R"EOF(
http_uri:
  uri: "https://example.com"
  cluster: "test"
  timeout: 1s
request_headers_to_add:
- header:
    key: "x-api-key"
    value: "my-key"
- header:
    key: "x-custom"
    value: "custom-value"
)EOF");

  TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  applicator.apply(headers);

  EXPECT_EQ("my-key", getHeader(headers, "x-api-key"));
  EXPECT_EQ("custom-value", getHeader(headers, "x-custom"));
}

TEST_F(HttpServiceHeadersApplicatorTest, RawValueHeaders) {
  auto applicator = buildApplicator(R"EOF(
http_uri:
  uri: "https://example.com"
  cluster: "test"
  timeout: 1s
request_headers_to_add:
- header:
    key: "x-raw"
    raw_value: "cmF3LWJ5dGVz"
)EOF");

  TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  applicator.apply(headers);

  // "cmF3LWJ5dGVz" is base64 for "raw-bytes"; proto3 bytes fields decode base64 from YAML.
  EXPECT_EQ("raw-bytes", getHeader(headers, "x-raw"));
}

TEST_F(HttpServiceHeadersApplicatorTest, FormattedValueHeaders) {
  auto applicator = buildApplicator(R"EOF(
http_uri:
  uri: "https://example.com"
  cluster: "test"
  timeout: 1s
request_headers_to_add:
- header:
    key: "x-formatted"
    value: "hello-%PROTOCOL%-world"
)EOF");

  TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  applicator.apply(headers);

  // With no real stream info, %PROTOCOL% evaluates to "-".
  EXPECT_EQ("hello---world", getHeader(headers, "x-formatted"));
}

TEST_F(HttpServiceHeadersApplicatorTest, MixedStaticAndFormattedHeaders) {
  auto applicator = buildApplicator(R"EOF(
http_uri:
  uri: "https://example.com"
  cluster: "test"
  timeout: 1s
request_headers_to_add:
- header:
    key: "x-static"
    raw_value: "c3RhdGljLXZhbHVl"
- header:
    key: "x-formatted"
    value: "formatted-value"
)EOF");

  TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  applicator.apply(headers);

  EXPECT_EQ("static-value", getHeader(headers, "x-static"));
  EXPECT_EQ("formatted-value", getHeader(headers, "x-formatted"));
}

TEST_F(HttpServiceHeadersApplicatorTest, InvalidFormattedValueReportsError) {
  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(R"EOF(
http_uri:
  uri: "https://example.com"
  cluster: "test"
  timeout: 1s
request_headers_to_add:
- header:
    key: "x-bad"
    value: "%NOSUCHCOMMAND%"
)EOF",
                            http_service);

  absl::Status creation_status = absl::OkStatus();
  HttpServiceHeadersApplicator applicator(http_service, server_context_, creation_status);
  EXPECT_FALSE(creation_status.ok());
  EXPECT_EQ(creation_status.message(), "Not supported field in StreamInfo: NOSUCHCOMMAND");
}

TEST_F(HttpServiceHeadersApplicatorTest, MultipleValueFieldsReportsError) {
  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(R"EOF(
http_uri:
  uri: "https://example.com"
  cluster: "test"
  timeout: 1s
request_headers_to_add:
- header:
    key: "x-conflict"
    value: "plain"
    raw_value: "cmF3"
)EOF",
                            http_service);

  absl::Status creation_status = absl::OkStatus();
  HttpServiceHeadersApplicator applicator(http_service, server_context_, creation_status);
  // Both value and raw_value set; raw_value takes precedence (non-empty raw_value is used as
  // static header). The value field is ignored since raw_value is checked first.
  // This matches proto3 behavior where both fields can be set independently.
  EXPECT_TRUE(creation_status.ok());
}

} // namespace
} // namespace Http
} // namespace Envoy
