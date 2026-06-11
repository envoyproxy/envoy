#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/listener/http_inspector/v3/http_inspector.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/extensions/filters/listener/http_inspector/http_inspector.h"

#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/server.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Ge;
namespace Envoy {

namespace {
void insertHttpInspectorConfigModifier(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  ::envoy::extensions::filters::listener::http_inspector::v3::HttpInspector http_inspector;
  auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
  auto* ppv_filter = listener->add_listener_filters();
  ppv_filter->set_name("http_inspector");
  ppv_filter->mutable_typed_config()->PackFrom(http_inspector);
}

std::string testParamToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, Http1ParserImpl>>&
        info) {
  ::testing::TestParamInfo<Network::Address::IpVersion> ip_info(std::get<0>(info.param),
                                                                info.index);
  return TestUtility::ipTestParamsToString(ip_info) + "_" +
         TestUtility::http1ParserImplToString(std::get<1>(info.param));
}

} // namespace

class HttpInspectorTcpIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, Http1ParserImpl>>,
      public BaseIntegrationTest {
public:
  HttpInspectorTcpIntegrationTest()
      : BaseIntegrationTest(std::get<0>(GetParam()), ConfigHelper::tcpProxyConfig()),
        parser_impl_(std::get<1>(GetParam())) {
    config_helper_.addConfigModifier(insertHttpInspectorConfigModifier);
    config_helper_.renameListener("tcp_proxy");

    if (parser_impl_ == Http1ParserImpl::BalsaParser) {
      scoped_runtime_.mergeValues(
          {{"envoy.reloadable_features.http_inspector_use_balsa_parser", "true"}});
    } else {
      scoped_runtime_.mergeValues(
          {{"envoy.reloadable_features.http_inspector_use_balsa_parser", "false"}});
    }
  }

  const Http1ParserImpl parser_impl_;
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(
    ParsersAndIp, HttpInspectorTcpIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(Http1ParserImpl::HttpParser, Http1ParserImpl::BalsaParser)),
    testParamToString);

TEST_P(HttpInspectorTcpIntegrationTest, DetectNoHttp) {
  initialize();

  std::string data = "hello";
  size_t expected_bytes = 5;
  if (parser_impl_ == Http1ParserImpl::BalsaParser) {
    data = "hello\r\n";
    expected_bytes = 7;
  }

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write(data, false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(expected_bytes));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  test_server_->waitForCounter("http_inspector.http_not_found", Ge(1));
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
  test_server_->waitForCounter("http_inspector.http11_found", Ge(1));
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
  test_server_->waitForCounter("http_inspector.http_not_found", Ge(1));
}

} // namespace Envoy
