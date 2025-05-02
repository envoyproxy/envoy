#include "test/integration/integration.h"

namespace Envoy {

static const std::string& filterConfig() {
  static std::string config = absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: ingress_http
            codec_type: AUTO
            route_config:
              name: local_route
              virtual_hosts:
                - name: local_service
                  domains: ["*"]
                  routes:
                    - match: { prefix: "/" }
                      route: { cluster: cluster_0 }
            http_filters:
              - name: envoy.filters.http.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF");
  return config;
}

class ParserIntegrationTest : public testing::Test, public BaseIntegrationTest {
public:
  ParserIntegrationTest()
      : BaseIntegrationTest(TestEnvironment::getIpVersionsForTest()[0], filterConfig()) {}

  virtual void setRuntimeVariants() {}

  void SetUp() override {
    setRuntimeVariants();
    autonomous_upstream_ = true;
    initialize();
    tcp_client_ = makeTcpConnection(lookupPort("listener_0"));
    ASSERT_TRUE(tcp_client_->connected());
  }

  void TearDown() override { tcp_client_->close(); }

protected:
  IntegrationTcpClientPtr tcp_client_;
};

class ParserIntegrationRuntimeAllowNewlinesFalseTest : public ParserIntegrationTest {
public:
  using ParserIntegrationTest::ParserIntegrationTest;
  void setRuntimeVariants() override {
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.http1_balsa_allow_cr_or_lf_at_request_start", "false");
  }
};

TEST_F(ParserIntegrationTest, NewlinesBetweenRequestsAreIgnored) {
  // Make two requests in a row, with a technically-incorrect newline between
  // them. Per RFC 9112, clients MUST NOT do this, but servers SHOULD tolerate it.
  ASSERT_TRUE(tcp_client_->write(
      "POST / HTTP/1.1\r\nHost: foo.lyft.com\r\nContent-Length: 5\r\n\r\naaaaa"
      "\r\nPOST / HTTP/1.1\r\nHost: foo.lyft.com\r\nContent-Length: 4\r\n\r\naaaa"));
  // If the decoder doesn't correctly handle unexpected newlines, the second
  // response is likely to be `400 Bad Request` instead of `200 OK`.
  EXPECT_TCP_RESPONSE(tcp_client_, testing::ContainsRegex("200 OK.*200 OK"));
}

TEST_F(ParserIntegrationRuntimeAllowNewlinesFalseTest,
       NewlinesBetweenRequestsAreAnErrorWithRuntimeFlag) {
  // Make two requests in a row, with a technically-incorrect newline between
  // them. Per RFC 9112, clients MUST NOT do this, but servers SHOULD tolerate it.
  ASSERT_TRUE(tcp_client_->write(
      "POST / HTTP/1.1\r\nHost: foo.lyft.com\r\nContent-Length: 5\r\n\r\naaaaa"
      "\r\nPOST / HTTP/1.1\r\nHost: foo.lyft.com\r\nContent-Length: 4\r\n\r\naaaa"));
  // If the decoder doesn't correctly handle unexpected newlines, the second
  // response is likely to be `400 Bad Request` instead of `200 OK`.
  EXPECT_TCP_RESPONSE(tcp_client_, testing::ContainsRegex("200 OK.*400 Bad Request"));
}

} // namespace Envoy
