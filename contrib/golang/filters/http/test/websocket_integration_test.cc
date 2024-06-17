#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/integration/utility.h"
#include "test/integration/websocket_integration_test.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "contrib/golang/filters/http/source/golang_filter.h"
#include "gtest/gtest.h"

namespace Envoy {

class GolangWebsocketIntegrationTest : public WebsocketIntegrationTest {
public:
  void cleanup() { Dso::DsoManager<Dso::HttpFilterDsoImpl>::cleanUpForTest(); }
};

INSTANTIATE_TEST_SUITE_P(Protocols, GolangWebsocketIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

ConfigHelper::HttpModifierFunction setRouteUsingWebsocket() {
  return [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { hcm.add_upgrade_configs()->set_upgrade_type("websocket"); };
}

void WebsocketIntegrationTest::initialize() { HttpProtocolIntegrationTest::initialize(); }

std::string genSoPath(std::string name) {
  return TestEnvironment::substitute(
      "{{ test_rundir }}/contrib/golang/filters/http/test/test_data/" + name + "/filter.so");
}

std::string filterConfig(const std::string& name) {
  const auto yaml_fmt = R"EOF(
name: golang
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config
  library_id: %s
  library_path: %s
  plugin_name: %s
  plugin_config:
    "@type": type.googleapis.com/xds.type.v3.TypedStruct
    value:
     echo_body: "echo from go"
     match_path: "/echo"
)EOF";

  return absl::StrFormat(yaml_fmt, name, genSoPath(name), name);
}

TEST_P(GolangWebsocketIntegrationTest, WebsocketGolangFilterChain) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  config_helper_.prependFilter(filterConfig("websocket"));
  config_helper_.skipPortUsageValidation();

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));

  // Send upgrade request without CL and TE headers
  ASSERT_TRUE(tcp_client->write(
      "GET / HTTP/1.1\r\nHost: host\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\n", false,
      false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));
  // Make sure Envoy did not add TE or CL headers
  ASSERT_FALSE(absl::StrContains(received_data, "content-length"));
  ASSERT_FALSE(absl::StrContains(received_data, "transfer-encoding"));
  // Make sure Golang plugin take affects
  ASSERT_TRUE(absl::StrContains(received_data, "test-websocket-req-key: foo"));
  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 101 Switching Protocols\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\n",
      false));

  tcp_client->waitForData("\r\n\r\n", false);
  // Make sure Envoy did not add TE or CL on the response path
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "content-length"));
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "transfer-encoding"));
  // Make sure Golang plugin take affects
  ASSERT_TRUE(absl::StrContains(tcp_client->data(), "test-websocket-rsp-key: bar"));

  fake_upstream_connection->clearData();
  // Send data and make sure Envoy did not add chunk framing
  ASSERT_TRUE(tcp_client->write("foo bar\r\n", false, false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("\r\n"),
                                                    &received_data));
  // Make sure Golang plugin take affects
  ASSERT_TRUE(absl::StrContains(received_data, "Hello_foo bar"));

  tcp_client->clearData();
  // Send response data and make sure Envoy did not add chunk framing on the response path
  ASSERT_TRUE(fake_upstream_connection->write("bar foo\r\n", false));
  tcp_client->waitForData("bar foo\r\n", false);
  // Make sure Golang plugin take affects
  ASSERT_TRUE(absl::StrContains(tcp_client->data(), "Bye_bar foo"));
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  cleanup();
}

} // namespace Envoy
