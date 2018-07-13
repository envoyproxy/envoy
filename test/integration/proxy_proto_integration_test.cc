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

TEST_P(ProxyProtoIntegrationTest, v1RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, v2RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                  0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                  0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfe, 0x04, 0xd2};
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, v1RouterRequestAndResponseWithBodyNoBufferV6) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP6 1:2:3::4 FE00:: 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, v2RouterRequestAndResponseWithBodyNoBufferV6) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                  0x0a, 0x21, 0x22, 0x00, 0x24, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
                                  0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02};
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
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
