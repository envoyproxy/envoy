#include <vector>

#include "source/extensions/filters/http/aws_lambda/request_response.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using source::extensions::filters::http::aws_lambda::Request;

namespace Envoy {
namespace {

class AwsLambdaFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  AwsLambdaFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void SetUp() override {
    // Set these environment variables to quickly sign credentials instead of attempting to query
    // instance metadata and timing-out.
    TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "aws-user", 1 /*overwrite*/);
    TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1 /*overwrite*/);
    TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1 /*overwrite*/);
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void TearDown() override { fake_upstream_connection_.reset(); }

  void addUpstreamProtocolOptions() {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
      protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
      protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
      ConfigHelper::setProtocolOptions(*cluster, protocol_options);
    });
  }

  void replaceRoute() {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          vhost->clear_routes();
          auto route = vhost->add_routes();
          auto* match = route->mutable_match();
          match->set_prefix("/api/lambda/");
          auto* action = route->mutable_route();
          action->set_cluster("cluster_0");
          action->set_prefix_rewrite("/new_path/");
          action->set_host_rewrite_literal("lambda.us-east-2.amazonaws.com");
        });
  }

  void setupLambdaFilter(bool passthrough, bool downstream) {
    constexpr absl::string_view filter =
        R"EOF(
            name: envoy.filters.http.aws_lambda
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.Config
              arn: "arn:aws:lambda:us-west-2:123456789:function:test"
              payload_passthrough: {}
            )EOF";
    config_helper_.prependFilter(fmt::format(filter, passthrough), downstream);

    if (!downstream) {
      addUpstreamProtocolOptions();
      replaceRoute();
    } else {
      constexpr auto metadata_yaml = R"EOF(
        com.amazonaws.lambda:
          egress_gateway: true
        )EOF";
      config_helper_.addClusterFilterMetadata(metadata_yaml);
    }
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

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    // verify headers
    expected_response_headers.iterate([actual_headers = &response->headers()](
                                          const Http::HeaderEntry& expected_entry) {
      const auto actual_entry = actual_headers->get(
          Http::LowerCaseString(std::string(expected_entry.key().getStringView())));
      EXPECT_EQ(actual_entry[0]->value().getStringView(), expected_entry.value().getStringView());
      return Http::HeaderMap::Iterate::Continue;
    });

    // verify cookies if we have any
    if (!expected_response_cookies.empty()) {
      std::vector<std::string> actual_cookies;
      response->headers().iterate([&actual_cookies](const Http::HeaderEntry& entry) {
        if (entry.key().getStringView() == Http::Headers::get().SetCookie.get()) {
          actual_cookies.emplace_back(entry.value().getStringView());
        }
        return Http::HeaderMap::Iterate::Continue;
      });

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

TEST_P(AwsLambdaFilterIntegrationTest, JsonWrappedHeaderOnlyRequestDownstream) {
  setupLambdaFilter(false /*passthrough*/, true);
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
  setupLambdaFilter(false /*passthrough*/, true);
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
  setupLambdaFilter(false /*passthrough*/, true);
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

TEST_P(AwsLambdaFilterIntegrationTest, UpstreamShouldBeProcessedAfterRoute) {
  setupLambdaFilter(false /*passthrough*/, false);
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/api/lambda/resize?type=jpg"},
                                                 {":authority", "host"},
                                                 {"s3-location", "mybucket/images/123.jpg"}};
  constexpr auto expected_json_request = R"EOF(
  {
    "rawPath": "/new_path/resize?type=jpg",
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

TEST_P(AwsLambdaFilterIntegrationTest, ExcludeHeadersFromSigning) {
  const std::string filter_config = R"EOF(
    name: envoy.filters.http.aws_lambda
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.Config
      arn: "arn:aws:lambda:us-west-2:123456789:function:test"
      payload_passthrough: true
      match_excluded_headers:
        - prefix: x-amzn
        - exact: x-custom-exclude
  )EOF";

  config_helper_.prependFilter(filter_config, true);

  constexpr auto metadata_yaml = R"EOF(
    com.amazonaws.lambda:
      egress_gateway: true
  )EOF";
  config_helper_.addClusterFilterMetadata(metadata_yaml);

  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/test"},
                                                 {":authority", "host"},
                                                 {"x-amzn-vpc-id", "vpc-12345"},
                                                 {"x-amzn-trace-id", "trace-abc"},
                                                 {"x-custom-exclude", "should-not-sign"},
                                                 {"x-custom-include", "should-sign"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Verify that Authorization header is present (signing occurred)
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("authorization")).empty());

  // Verify excluded headers are still forwarded but not in signed headers
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amzn-vpc-id")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amzn-trace-id")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-custom-exclude")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-custom-include")).empty());

  // Get the Authorization header to verify excluded headers are not in SignedHeaders
  auto auth_header = upstream_request_->headers().get(Http::LowerCaseString("authorization"));
  ASSERT_FALSE(auth_header.empty());
  std::string auth_value(auth_header[0]->value().getStringView());

  // Verify that custom headers and x-custom-exclude are not in SignedHeaders
  EXPECT_THAT(auth_value, testing::Not(testing::HasSubstr("x-amzn-vpc-id")));
  EXPECT_THAT(auth_value, testing::Not(testing::HasSubstr("x-amzn-trace-id")));
  EXPECT_THAT(auth_value, testing::Not(testing::HasSubstr("x-custom-exclude")));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(AwsLambdaFilterIntegrationTest, IncludeHeadersInSigning) {
  const std::string filter_config = R"EOF(
    name: envoy.filters.http.aws_lambda
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.Config
      arn: "arn:aws:lambda:us-west-2:123456789:function:test"
      payload_passthrough: true
      match_included_headers:
        - prefix: x-custom
        - exact: user-agent
  )EOF";

  config_helper_.prependFilter(filter_config, true);

  constexpr auto metadata_yaml = R"EOF(
    com.amazonaws.lambda:
      egress_gateway: true
  )EOF";
  config_helper_.addClusterFilterMetadata(metadata_yaml);

  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "POST"},
                                                 {":path", "/test"},
                                                 {":authority", "host"},
                                                 {"x-custom-header", "custom-value"},
                                                 {"user-agent", "test-agent"},
                                                 {"x-other-header", "other-value"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Verify that Authorization header is present (signing occurred)
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("authorization")).empty());

  // Verify all headers are still forwarded
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-custom-header")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("user-agent")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-other-header")).empty());

  // Get the Authorization header to verify only included headers are in SignedHeaders
  auto auth_header = upstream_request_->headers().get(Http::LowerCaseString("authorization"));
  ASSERT_FALSE(auth_header.empty());
  std::string auth_value(auth_header[0]->value().getStringView());

  // Verify that included headers are in SignedHeaders
  EXPECT_THAT(auth_value, testing::HasSubstr("x-custom-header"));
  EXPECT_THAT(auth_value, testing::HasSubstr("user-agent"));

  // Verify that non-included headers are not in SignedHeaders (except required headers like host)
  EXPECT_THAT(auth_value, testing::Not(testing::HasSubstr("x-other-header")));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(AwsLambdaFilterIntegrationTest, ExcludeHeadersUpstream) {
  const std::string filter_config = R"EOF(
    name: envoy.filters.http.aws_lambda
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.Config
      arn: "arn:aws:lambda:us-west-2:123456789:function:test"
      payload_passthrough: true
      match_excluded_headers:
        - prefix: x-amzn
  )EOF";

  config_helper_.prependFilter(filter_config, false);
  addUpstreamProtocolOptions();

  constexpr auto metadata_yaml = R"EOF(
    com.amazonaws.lambda:
      egress_gateway: true
  )EOF";
  config_helper_.addClusterFilterMetadata(metadata_yaml);

  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/test"},
                                                 {":authority", "host"},
                                                 {"x-amzn-vpc-id", "vpc-12345"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Verify that Authorization header is present
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("authorization")).empty());

  // Verify custom header is still forwarded
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amzn-vpc-id")).empty());

  // Get the Authorization header to verify custom header is not in SignedHeaders
  auto auth_header = upstream_request_->headers().get(Http::LowerCaseString("authorization"));
  ASSERT_FALSE(auth_header.empty());
  std::string auth_value(auth_header[0]->value().getStringView());
  EXPECT_THAT(auth_value, testing::Not(testing::HasSubstr("x-amzn-vpc-id")));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

} // namespace
} // namespace Envoy
