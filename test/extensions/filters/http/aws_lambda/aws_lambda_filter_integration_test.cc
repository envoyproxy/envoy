#include <vector>

#include "source/extensions/filters/http/aws_lambda/request_response.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using source::extensions::filters::http::aws_lambda::Request;

namespace Envoy {
namespace {

class AwsLambdaFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  AwsLambdaFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override {
    // Set these environment variables to quickly sign credentials instead of attempting to query
    // instance metadata and timing-out.
    TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "aws-user", 1 /*overwrite*/);
    TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1 /*overwrite*/);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  }

  void TearDown() override { fake_upstream_connection_.reset(); }

  void setupLambdaFilter(bool passthrough) {
    const std::string filter =
        R"EOF(
            name: envoy.filters.http.aws_lambda
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.Config
              arn: "arn:aws:lambda:us-west-2:123456789:function:test"
              payload_passthrough: {}
            )EOF";
    config_helper_.addFilter(fmt::format(filter, passthrough));

    constexpr auto metadata_yaml = R"EOF(
        com.amazonaws.lambda:
          egress_gateway: true
        )EOF";
    config_helper_.addClusterFilterMetadata(metadata_yaml);
  }

  template <typename TMap>
  ABSL_MUST_USE_RESULT testing::AssertionResult compareMaps(const TMap& m1, const TMap& m2) {
    for (auto&& kvp : m1) {
      auto it = m2.find(kvp.first);
      if (it == m2.end()) {
        return AssertionFailure() << "Failed to find value: " << kvp.first;
        ;
      }
      if (it->second != kvp.second) {
        return AssertionFailure() << "Values of key: " << kvp.first
                                  << " are different. expected: " << kvp.second
                                  << " actual: " << it->second;
      }
    }
    return AssertionSuccess();
  }

  void runTest(const Http::RequestHeaderMap& request_headers, const std::string& request_body,
               const std::string& expected_json_request,
               const Http::ResponseHeaderMap& lambda_response_headers,
               const std::string& lambda_response_body,
               const Http::ResponseHeaderMap& expected_response_headers,
               const std::vector<std::string>& expected_response_cookies,
               const std::string& expected_response_body) {

    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response;
    if (request_body.empty()) {
      response = codec_client_->makeHeaderOnlyRequest(request_headers);
    } else {
      auto encoder_decoder = codec_client_->startRequest(request_headers);
      request_encoder_ = &encoder_decoder.first;
      response = std::move(encoder_decoder.second);
      // Chunk the data to simulate a real request.
      const size_t chunk_size = 5;
      size_t i = 0;
      for (; i < request_body.length() / chunk_size; i++) {
        Buffer::OwnedImpl buffer(request_body.substr(i * chunk_size, chunk_size));
        codec_client_->sendData(*request_encoder_, buffer, false);
      }
      // Send the last chunk flagged as end_stream.
      Buffer::OwnedImpl buffer(
          request_body.substr(i * chunk_size, request_body.length() % chunk_size));
      codec_client_->sendData(*request_encoder_, buffer, true);
    }

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    Request transformed_request;
    Request expected_request;
    TestUtility::loadFromJson(upstream_request_->body().toString(), transformed_request);
    TestUtility::loadFromJson(expected_json_request, expected_request);

    EXPECT_EQ(expected_request.raw_path(), transformed_request.raw_path());
    EXPECT_EQ(expected_request.method(), transformed_request.method());
    EXPECT_EQ(expected_request.body(), transformed_request.body());
    EXPECT_EQ(expected_request.is_base64_encoded(), transformed_request.is_base64_encoded());
    EXPECT_TRUE(compareMaps(expected_request.headers(), transformed_request.headers()));
    EXPECT_TRUE(compareMaps(expected_request.query_string_parameters(),
                            transformed_request.query_string_parameters()));

    if (lambda_response_body.empty()) {
      upstream_request_->encodeHeaders(lambda_response_headers, true /*end_stream*/);
    } else {
      upstream_request_->encodeHeaders(lambda_response_headers, false /*end_stream*/);
      Buffer::OwnedImpl buffer(lambda_response_body);
      upstream_request_->encodeData(buffer, true);
    }

    response->waitForEndStream();
    EXPECT_TRUE(response->complete());

    // verify headers
    expected_response_headers.iterate(
        [](const Http::HeaderEntry& expected_entry, void* ctx) {
          const auto* actual_headers = static_cast<const Http::ResponseHeaderMap*>(ctx);
          const auto* actual_entry = actual_headers->get(
              Http::LowerCaseString(std::string(expected_entry.key().getStringView())));
          EXPECT_EQ(actual_entry->value().getStringView(), expected_entry.value().getStringView());
          return Http::HeaderMap::Iterate::Continue;
        },
        // Because headers() returns a pointer to const we have to cast it
        // away to match the callback signature. This is safe because we do
        // not call any non-const functions on the headers in the callback.
        const_cast<Http::ResponseHeaderMap*>(&response->headers()));

    // verify cookies if we have any
    if (!expected_response_cookies.empty()) {
      std::vector<std::string> actual_cookies;
      response->headers().iterate(
          [](const Http::HeaderEntry& entry, void* ctx) {
            auto* list = static_cast<std::vector<std::string>*>(ctx);
            if (entry.key().getStringView() == Http::Headers::get().SetCookie.get()) {
              list->emplace_back(entry.value().getStringView());
            }
            return Http::HeaderMap::Iterate::Continue;
          },
          &actual_cookies);

      EXPECT_EQ(expected_response_cookies, actual_cookies);
    }

    // verify body
    EXPECT_STREQ(expected_response_body.c_str(), response->body().c_str());

    // cleanup
    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AwsLambdaFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AwsLambdaFilterIntegrationTest, JsonWrappedHeaderOnlyRequest) {
  setupLambdaFilter(false /*passthrough*/);
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/resize?type=jpg"},
                                                 {":authority", "host"},
                                                 {"s3-location", "mybucket/images/123.jpg"}};
  constexpr auto expected_json_request = R"EOF(
  {
    "rawPath": "/resize?type=jpg",
    "method": "GET",
    "headers":{ "s3-location": "mybucket/images/123.jpg"},
    "queryStringParameters": {"type":"jpg"},
    "body": "",
    "isBase64Encoded": false
  }
  )EOF";

  const std::string lambda_response_body = R"EOF(
  {
      "body": "my-bucket/123-small.jpg",
      "isBase64Encoded": false,
      "statusCode": 200,
      "cookies": ["user=John", "session-id=1337"],
      "headers": {"x-amz-custom-header": "envoy,proxy"}
  }
  )EOF";

  Http::TestResponseHeaderMapImpl lambda_response_headers{
      {":status", "201"},
      {"content-type", "application/json"},
      {"content-length", fmt::format("{}", lambda_response_body.length())}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                            {"content-type", "application/json"},
                                                            {"x-amz-custom-header", "envoy,proxy"}};
  std::vector<std::string> expected_response_cookies{"user=John", "session-id=1337"};
  constexpr auto expected_response_body = "my-bucket/123-small.jpg";
  runTest(request_headers, "" /*request_body*/, expected_json_request, lambda_response_headers,
          lambda_response_body, expected_response_headers, expected_response_cookies,
          expected_response_body);
}

TEST_P(AwsLambdaFilterIntegrationTest, JsonWrappedPlainBody) {
  setupLambdaFilter(false /*passthrough*/);
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/resize?type=jpg"},
                                                 {":authority", "host"},
                                                 {"content-type", "text/plain"},
                                                 {"xray-trace-id", "qwerty12345"}};

  constexpr auto request_body = "AWS Lambda is a FaaS platform";

  constexpr auto expected_json_request = R"EOF(
  {
    "rawPath": "/resize?type=jpg",
    "method": "GET",
    "headers":{ "xray-trace-id": "qwerty12345"},
    "queryStringParameters": {"type":"jpg"},
    "body": "AWS Lambda is a FaaS platform",
    "isBase64Encoded": false
  }
  )EOF";

  const std::string lambda_response_body = R"EOF(
  {
      "body": "AWS Lambda is cheap!",
      "isBase64Encoded": false,
      "statusCode": 200,
      "cookies": ["user=John", "session-id=1337"],
      "headers": {"x-amz-custom-header": "envoy,proxy"}
  }
  )EOF";

  Http::TestResponseHeaderMapImpl lambda_response_headers{
      {":status", "201"},
      {"content-type", "application/json"},
      {"content-length", fmt::format("{}", lambda_response_body.length())}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                            {"content-type", "application/json"},
                                                            {"x-amz-custom-header", "envoy,proxy"}};
  std::vector<std::string> expected_response_cookies{"user=John", "session-id=1337"};
  constexpr auto expected_response_body = "AWS Lambda is cheap!";
  runTest(request_headers, request_body, expected_json_request, lambda_response_headers,
          lambda_response_body, expected_response_headers, expected_response_cookies,
          expected_response_body);
}

TEST_P(AwsLambdaFilterIntegrationTest, JsonWrappedBinaryBody) {
  setupLambdaFilter(false /*passthrough*/);
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/resize?type=jpg"},
                                                 {":authority", "host"},
                                                 {"content-type", "application/octet-stream"},
                                                 {"xray-trace-id", "qwerty12345"}};

  constexpr auto request_body = "this should get base64 encoded";

  constexpr auto expected_json_request = R"EOF(
  {
    "rawPath": "/resize?type=jpg",
    "method": "GET",
    "headers":{ "xray-trace-id": "qwerty12345"},
    "queryStringParameters": {"type":"jpg"},
    "body": "dGhpcyBzaG91bGQgZ2V0IGJhc2U2NCBlbmNvZGVk",
    "isBase64Encoded": true
  }
  )EOF";

  const std::string lambda_response_body = R"EOF(
  {
      "body": "QVdTIExhbWJkYSBpcyBjaGVhcCE=",
      "isBase64Encoded": true,
      "statusCode": 200,
      "cookies": ["user=John", "session-id=1337"],
      "headers": {"x-amz-custom-header": "envoy,proxy"}
  }
  )EOF";

  Http::TestResponseHeaderMapImpl lambda_response_headers{
      {":status", "201"},
      {"content-type", "application/json"},
      {"content-length", fmt::format("{}", lambda_response_body.length())}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                            {"content-type", "application/json"},
                                                            {"x-amz-custom-header", "envoy,proxy"}};
  std::vector<std::string> expected_response_cookies{"user=John", "session-id=1337"};
  constexpr auto expected_response_body = "AWS Lambda is cheap!";
  runTest(request_headers, request_body, expected_json_request, lambda_response_headers,
          lambda_response_body, expected_response_headers, expected_response_cookies,
          expected_response_body);
}

} // namespace
} // namespace Envoy
