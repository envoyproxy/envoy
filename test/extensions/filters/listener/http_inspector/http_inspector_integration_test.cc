#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/listener/http_inspector/v3/http_inspector.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/extensions/filters/listener/http_inspector/http_inspector.h"

#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/server.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace {
void insertHttpInspectorConfigModifier(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  ::envoy::extensions::filters::listener::http_inspector::v3::HttpInspector http_inspector;
  auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
  auto* ppv_filter = listener->add_listener_filters();
  ppv_filter->set_name("http_inspector");
  ppv_filter->mutable_typed_config()->PackFrom(http_inspector);
}
} // namespace

class HttpInspectorTcpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public BaseIntegrationTest {
public:
  HttpInspectorTcpIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    config_helper_.addConfigModifier(insertHttpInspectorConfigModifier);
    config_helper_.renameListener("tcp_proxy");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpInspectorTcpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HttpInspectorTcpIntegrationTest, DetectNoHttp) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  test_server_->waitForCounterGe("http_inspector.http_not_found", 1);
}

TEST_P(HttpInspectorTcpIntegrationTest, DetectHttp) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  const std::string basic_request = "GET / HTTP/1.1\r\nHost: foo\r\ncontent-length: 0\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(basic_request, false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(basic_request.size()));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  test_server_->waitForCounterGe("http_inspector.http11_found", 1);
}

// Tests that the inspector makes a decision when CRLF is read.
// Without CRLF, the inspector blocks until either the max bytes are read or a timeout is hit.
TEST_P(HttpInspectorTcpIntegrationTest, DetectCRLF) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("GET\r\n", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  test_server_->waitForCounterGe("http_inspector.http_not_found", 1);
}

} // namespace Envoy
