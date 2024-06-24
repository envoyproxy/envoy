#include "source/common/common/fmt.h"
#include "source/extensions/common/aws/utility.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {

using Envoy::Extensions::Common::Aws::Utility;

class AwsMetadataIntegrationTestBase : public ::testing::Test, public BaseIntegrationTest {
public:
  AwsMetadataIntegrationTestBase(int status_code, int delay_s)
      : BaseIntegrationTest(Network::Address::IpVersion::v4, renderConfig(status_code, delay_s)) {}

  static std::string renderConfig(int status_code, int delay_s) {
    return absl::StrCat(ConfigHelper::baseConfig(),
                        fmt::format(R"EOF(
    filter_chains:
      filters:
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: metadata_test
          http_filters:
            - name: fault
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
                delay:
                  fixed_delay:
                    seconds: {}
                    nanos: {}
                  percentage:
                    numerator: 100
                    denominator: HUNDRED
            - name: envoy.filters.http.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
          codec_type: HTTP1
          route_config:
            virtual_hosts:
              name: metadata_endpoint
              routes:
                - name: redirect_route
                  redirect:
                    prefix_rewrite: "/"
                  match:
                    prefix: "/redirect"
                - name: put_token_route
                  direct_response:
                    status: {}
                    body:
                      inline_string: TOKEN_VALUE
                  match:
                    prefix: "/"
                    headers:
                      - name: ":method"
                        string_match:
                          exact: PUT
                      - name: X-aws-ec2-metadata-token-ttl-seconds
                        string_match:
                          exact: "21600"
                - name: auth_route
                  direct_response:
                    status: {}
                    body:
                      inline_string: METADATA_VALUE_WITH_AUTH
                  match:
                    prefix: "/"
                    headers:
                      - name: Authorization
                        string_match:
                          exact: AUTH_TOKEN
                - name: no_auth_route
                  direct_response:
                    status: {}
                    body:
                      inline_string: METADATA_VALUE
                  match:
                    prefix: "/"
              domains: "*"
            name: route_config_0
      )EOF",
                                    delay_s, delay_s > 0 ? 0 : 1000, status_code, status_code,
                                    status_code));
  }

  void SetUp() override { BaseIntegrationTest::initialize(); }
};

class AwsMetadataIntegrationTestSuccess : public AwsMetadataIntegrationTestBase {
public:
  AwsMetadataIntegrationTestSuccess() : AwsMetadataIntegrationTestBase(200, 0) {}
};

TEST_F(AwsMetadataIntegrationTestSuccess, Success) {
  const auto authority = fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(version_),
                                     lookupPort("listener_0"));
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
      {":path", "/"}, {":authority", authority}, {":scheme", "http"}, {":method", "GET"}}};
  Http::RequestMessageImpl message(std::move(headers));
  const auto response = Utility::fetchMetadata(message);

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ("METADATA_VALUE", *response);

  ASSERT_NE(nullptr, test_server_->counter("http.metadata_test.downstream_rq_completed"));
  EXPECT_EQ(1, test_server_->counter("http.metadata_test.downstream_rq_completed")->value());
}

TEST_F(AwsMetadataIntegrationTestSuccess, AuthToken) {
  const auto authority = fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(version_),
                                     lookupPort("listener_0"));
  auto headers = Http::RequestHeaderMapPtr{
      new Http::TestRequestHeaderMapImpl{{":path", "/"},
                                         {":authority", authority},
                                         {":scheme", "http"},
                                         {":method", "GET"},
                                         {"authorization", "AUTH_TOKEN"}}};
  Http::RequestMessageImpl message(std::move(headers));
  const auto response = Utility::fetchMetadata(message);

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ("METADATA_VALUE_WITH_AUTH", *response);

  ASSERT_NE(nullptr, test_server_->counter("http.metadata_test.downstream_rq_completed"));
  EXPECT_EQ(1, test_server_->counter("http.metadata_test.downstream_rq_completed")->value());
}

TEST_F(AwsMetadataIntegrationTestSuccess, FetchTokenHttpPut) {
  const auto authority = fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(version_),
                                     lookupPort("listener_0"));
  auto headers = Http::RequestHeaderMapPtr{
      new Http::TestRequestHeaderMapImpl{{":path", "/"},
                                         {":authority", authority},
                                         {":scheme", "http"},
                                         {":method", "PUT"},
                                         {"X-aws-ec2-metadata-token-ttl-seconds", "21600"}}};
  Http::RequestMessageImpl message(std::move(headers));
  const auto response = Utility::fetchMetadata(message);

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ("TOKEN_VALUE", *response);

  ASSERT_NE(nullptr, test_server_->counter("http.metadata_test.downstream_rq_completed"));
  // We explicitly disable the "Expect:" header while making PUT call,
  // so the number of requests will be counted as only 1.
  EXPECT_EQ(1, test_server_->counter("http.metadata_test.downstream_rq_completed")->value());
}

TEST_F(AwsMetadataIntegrationTestSuccess, Redirect) {
  const auto authority = fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(version_),
                                     lookupPort("listener_0"));
  auto headers = Http::RequestHeaderMapPtr{
      new Http::TestRequestHeaderMapImpl{{":path", "/redirect"},
                                         {":authority", authority},
                                         {":scheme", "http"},
                                         {":method", "GET"},
                                         {"authorization", "AUTH_TOKEN"}}};
  Http::RequestMessageImpl message(std::move(headers));
  const auto response = Utility::fetchMetadata(message);

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ("METADATA_VALUE_WITH_AUTH", *response);

  // We should make 2 requests, 1 that results in a redirect, and a final successful one
  ASSERT_NE(nullptr, test_server_->counter("http.metadata_test.downstream_rq_completed"));
  EXPECT_EQ(2, test_server_->counter("http.metadata_test.downstream_rq_completed")->value());

  ASSERT_NE(nullptr, test_server_->counter("http.metadata_test.downstream_rq_3xx"));
  EXPECT_EQ(1, test_server_->counter("http.metadata_test.downstream_rq_3xx")->value());
}

class AwsMetadataIntegrationTestFailure : public AwsMetadataIntegrationTestBase {
public:
  AwsMetadataIntegrationTestFailure() : AwsMetadataIntegrationTestBase(503, 0) {}
};

TEST_F(AwsMetadataIntegrationTestFailure, Failure) {
  const auto authority = fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(version_),
                                     lookupPort("listener_0"));
  auto headers = Http::RequestHeaderMapPtr{
      new Http::TestRequestHeaderMapImpl{{":path", "/"},
                                         {":authority", authority},
                                         {":scheme", "http"},
                                         {":method", "GET"},
                                         {"authorization", "AUTH_TOKEN"}}};

  Http::RequestMessageImpl message(std::move(headers));
  const auto start_time = timeSystem().monotonicTime();
  const auto response = Utility::fetchMetadata(message);
  const auto end_time = timeSystem().monotonicTime();

  EXPECT_FALSE(response.has_value());

  // Verify correct number of retries
  ASSERT_NE(nullptr, test_server_->counter("http.metadata_test.downstream_rq_completed"));
  EXPECT_EQ(4, test_server_->counter("http.metadata_test.downstream_rq_completed")->value());

  // Verify correct sleep time between retries: 4 * 1000 = 4000
  EXPECT_LE(4000,
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
}

class AwsMetadataIntegrationTestTimeout : public AwsMetadataIntegrationTestBase {
public:
  AwsMetadataIntegrationTestTimeout() : AwsMetadataIntegrationTestBase(200, 10) {}
};

TEST_F(AwsMetadataIntegrationTestTimeout, Timeout) {
  const auto authority = fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(version_),
                                     lookupPort("listener_0"));
  auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
      {":path", "/"}, {":authority", authority}, {":scheme", "http"}, {":method", "GET"}}};
  Http::RequestMessageImpl message(std::move(headers));

  const auto start_time = timeSystem().monotonicTime();
  const auto response = Utility::fetchMetadata(message);
  const auto end_time = timeSystem().monotonicTime();

  EXPECT_FALSE(response.has_value());

  // We do not check http.metadata_test.downstream_rq_completed value here because it's
  // behavior is different between Linux and Mac when Curl disconnects on timeout. On Mac it is
  // incremented, while on Linux it is not.

  // Verify correct sleep time between retries: 4 * 5000 = 20000
  EXPECT_LE(20000,
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
  EXPECT_GT(40000,
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
}

} // namespace Envoy
