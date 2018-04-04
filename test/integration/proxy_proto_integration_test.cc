#include "test/integration/proxy_proto_integration_test.h"

#include "common/buffer/buffer_impl.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, ProxyProtoIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(ProxyProtoIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, RouterRequestAndResponseWithBodyNoBufferV6) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP6 1:2:3::4 FE00:: 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, RouterProxyUnknownRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY UNKNOWN\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, RouterProxyUnknownLongRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY UNKNOWN 1:2:3::4 FE00:: 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, OriginalDst) {
  // Change the cluster to an original destination cluster. An original destination cluster
  // ignores the configured hosts, and instead uses the restored destination address from the
  // incoming (server) connection as the destination address for the outgoing (client) connection.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->set_type(envoy::api::v2::Cluster::ORIGINAL_DST);
    cluster->set_lb_policy(envoy::api::v2::Cluster::ORIGINAL_DST_LB);
  });

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // Create proxy protocol line that has the fake upstream address as the destination address.
    // This address will become the "restored" address for the server connection and will
    // be used as the destination address by the original destination cluster.
    std::string proxyLine = fmt::format(
        "PROXY {} {} 65535 {}\r\n",
        GetParam() == Network::Address::IpVersion::v4 ? "TCP4 1.2.3.4" : "TCP6 1:2:3::4",
        Network::Test::getLoopbackAddressString(GetParam()),
        fake_upstreams_[0]->localAddress()->ip()->port());

    Buffer::OwnedImpl buf(proxyLine);
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

} // namespace Envoy
