#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

constexpr char kRustModulesPath[] =
    "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust";
constexpr char kReferenceModule[] = "transport_socket_integration_test";
constexpr uint8_t kXorKey = 0x5a;

std::string xorString(absl::string_view in, uint8_t key) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    out.push_back(static_cast<char>(static_cast<uint8_t>(c) ^ key));
  }
  return out;
}

class DynamicModuleTransportSocketIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  DynamicModuleTransportSocketIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    skip_tag_extraction_rule_check_ = true;
    enableHalfClose(true);
  }

protected:
  static envoy::config::core::v3::TransportSocket buildTransportSocket(const std::string& name) {
    envoy::config::core::v3::TransportSocket transport_socket;
    transport_socket.set_name("envoy.transport_sockets.dynamic_modules");
    envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleTransportSocket config;
    config.mutable_dynamic_module_config()->set_name(kReferenceModule);
    config.set_transport_socket_name(name);
    transport_socket.mutable_typed_config()->PackFrom(config);
    return transport_socket;
  }

  void initializeWithDownstreamSocket(const std::string& name) {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
    config_helper_.addConfigModifier([name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      *filter_chain->mutable_transport_socket() = buildTransportSocket(name);
    });
    BaseIntegrationTest::initialize();
  }

  void initializeWithUpstreamSocket(const std::string& name) {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kRustModulesPath), 1);
    config_helper_.addConfigModifier([name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      *cluster->mutable_transport_socket() = buildTransportSocket(name);
    });
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleTransportSocketIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The passthrough socket forwards bytes unchanged in both directions.
TEST_P(DynamicModuleTransportSocketIntegrationTest, DownstreamPassthrough) {
  initializeWithDownstreamSocket("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("hello", false));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &upstream_data));
  EXPECT_EQ("hello", upstream_data);

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// The XOR socket transforms bytes in both directions on the downstream connection.
TEST_P(DynamicModuleTransportSocketIntegrationTest, DownstreamXorTransform) {
  initializeWithDownstreamSocket("xor");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // The downstream read path decrypts client bytes before they reach the upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &upstream_data));
  EXPECT_EQ(xorString("hello", kXorKey), upstream_data);

  // The downstream write path encrypts upstream bytes before they reach the client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData(xorString("world", kXorKey));

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// The XOR socket transforms bytes in both directions on the upstream connection.
TEST_P(DynamicModuleTransportSocketIntegrationTest, UpstreamXorTransform) {
  initializeWithUpstreamSocket("xor");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // The upstream write path encrypts client bytes before they reach the upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &upstream_data));
  EXPECT_EQ(xorString("hello", kXorKey), upstream_data);

  // The upstream read path decrypts upstream bytes before they reach the client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData(xorString("world", kXorKey));

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// Large transfers exercise the read and write loops across multiple I/O operations, including the
// write buffer length check.
TEST_P(DynamicModuleTransportSocketIntegrationTest, DownstreamLargeData) {
  initializeWithDownstreamSocket("xor");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  const std::string payload(100000, 'x');
  ASSERT_TRUE(tcp_client->write(payload, true));
  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(payload.size(), &upstream_data));
  EXPECT_EQ(xorString(payload, kXorKey), upstream_data);
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

} // namespace
} // namespace Envoy
