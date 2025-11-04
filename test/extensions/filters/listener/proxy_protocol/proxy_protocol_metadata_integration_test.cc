#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"

#include "test/extensions/filters/listener/proxy_protocol/proxy_proto_integration_test.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

// Integration test that verifies PROXY protocol works correctly with metadata-only addresses.
class ProxyProtocolMetadataIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ProxyProtocolMetadataIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {

    // Add PROXY protocol listener filter.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ::envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proxy_protocol;

      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* ppv_filter = listener->add_listener_filters();
      ppv_filter->set_name("envoy.listener.proxy_protocol");
      ppv_filter->mutable_typed_config()->PackFrom(proxy_protocol);
    });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolMetadataIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that PROXY protocol v1 with IPv4 addresses works as metadata.
TEST_P(ProxyProtocolMetadataIntegrationTest, V1Ipv4AsMetadata) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // Send PROXY protocol v1 header with IPv4 addresses.
    Buffer::OwnedImpl buf("PROXY TCP4 203.0.113.7 198.51.100.22 35646 80\r\n");
    conn->write(buf, false);
    return conn;
  };

  // The test should succeed even if OS doesn't support IPv4, because we parse as metadata.
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test that PROXY protocol v1 with IPv6 addresses works as metadata.
TEST_P(ProxyProtocolMetadataIntegrationTest, V1Ipv6AsMetadata) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // Send PROXY protocol v1 header with IPv6 addresses.
    Buffer::OwnedImpl buf("PROXY TCP6 2001:db8::1 2001:db8::2 35646 80\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test that PROXY protocol v2 with IPv4 addresses works as metadata.
TEST_P(ProxyProtocolMetadataIntegrationTest, V2Ipv4AsMetadata) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // PROXY protocol v2 header for IPv4: 203.0.113.7:35646 -> 198.51.100.22:80
    constexpr uint8_t buffer[] = {// Signature
                                  0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                  0x0a,
                                  // Version 2 | PROXY command
                                  0x21,
                                  // AF_INET | STREAM
                                  0x11,
                                  // Address length (12 bytes for IPv4)
                                  0x00, 0x0c,
                                  // Source address: 203.0.113.7
                                  0xcb, 0x00, 0x71, 0x07,
                                  // Destination address: 198.51.100.22
                                  0xc6, 0x33, 0x64, 0x16,
                                  // Source port: 35646
                                  0x8b, 0x3e,
                                  // Destination port: 80
                                  0x00, 0x50};
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test that PROXY protocol v2 with IPv6 addresses works as metadata.
TEST_P(ProxyProtocolMetadataIntegrationTest, V2Ipv6AsMetadata) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // PROXY protocol v2 header for IPv6
    constexpr uint8_t buffer[] = {// Signature
                                  0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                  0x0a,
                                  // Version 2 | PROXY command
                                  0x21,
                                  // AF_INET6 | STREAM
                                  0x21,
                                  // Address length (36 bytes for IPv6)
                                  0x00, 0x24,
                                  // Source address: 2001:db8::1 (16 bytes)
                                  0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x01,
                                  // Destination address: 2001:db8::2 (16 bytes)
                                  0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x02,
                                  // Source port: 35646
                                  0x8b, 0x3e,
                                  // Destination port: 80
                                  0x00, 0x50};
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test mixed address families to demonstrate metadata-only parsing.
// This test sends IPv4 PROXY protocol headers regardless of listener IP version,
// shows that addresses are parsed as metadata without OS validation.
TEST_P(ProxyProtocolMetadataIntegrationTest, MixedAddressFamilies) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));

    // Always send IPv4 addresses via PROXY protocol, regardless of listener version.
    // This works because addresses are parsed as metadata, not requiring OS support.
    Buffer::OwnedImpl buf("PROXY TCP4 203.0.113.7 198.51.100.22 35646 80\r\n");
    conn->write(buf, false);
    return conn;
  };

  // This should work even on IPv6-only systems because we parse addresses as metadata.
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test that verifies addresses from PROXY protocol are available for logging/routing.
TEST_P(ProxyProtocolMetadataIntegrationTest, AddressesAvailableForUse) {
  // Configure listener access log to verify addresses are parsed correctly.
  useListenerAccessLog("%DOWNSTREAM_REMOTE_ADDRESS% %DOWNSTREAM_LOCAL_ADDRESS%");

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP4 192.0.2.1 192.0.2.2 12345 443\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  cleanupUpstreamAndDownstream();

  // Verify the access log shows the PROXY protocol addresses.
  const std::string log_line = waitForAccessLog(listener_access_log_name_);
  EXPECT_TRUE(log_line.find("192.0.2.1:12345") != std::string::npos)
      << "Expected source address from PROXY protocol in log: " << log_line;
  EXPECT_TRUE(log_line.find("192.0.2.2:443") != std::string::npos)
      << "Expected destination address from PROXY protocol in log: " << log_line;
}

} // namespace Envoy
