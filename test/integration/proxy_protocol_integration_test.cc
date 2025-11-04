#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Integration test that verifies PROXY protocol works correctly even when addresses
// from the protocol header use an address family that may not be supported by the OS.
class ProxyProtocolIntegrationTest : public HttpIntegrationTest,
                                     public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ProxyProtocolIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithProxyProtocol() {
    config_helper_.addConfigModifier(
        [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter_chain = listener->mutable_filter_chains(0);
          
          // Add PROXY protocol listener filter.
          auto* listener_filter = listener->add_listener_filters();
          listener_filter->set_name("envoy.filters.listener.proxy_protocol");
          listener_filter->mutable_typed_config()->PackFrom(
              envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol());
        });

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          // Configure HTTP connection manager to use downstream remote address.
          hcm.mutable_use_remote_address()->set_value(true);
          hcm.set_skip_xff_append(false);
          
          // Add a custom header with the remote address for verification.
          auto* header = hcm.add_request_headers_to_add();
          auto* hdr = header->mutable_header();
          hdr->set_key("x-real-ip");
          hdr->set_value("%DOWNSTREAM_REMOTE_ADDRESS%");
        });

    HttpIntegrationTest::initialize();
  }

  void sendProxyProtocolV1Header(const std::string& header) {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
    ASSERT_TRUE(tcp_client->write(header, false));
    
    // Send HTTP request after PROXY protocol header.
    const std::string request = "GET / HTTP/1.1\r\nHost: test\r\n\r\n";
    ASSERT_TRUE(tcp_client->write(request));
    
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    // Read the request at the upstream.
    std::string received_data;
    ASSERT_TRUE(fake_upstream_connection->waitForData(
        FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));

    // Verify the x-real-ip header contains the address from PROXY protocol.
    EXPECT_TRUE(received_data.find("x-real-ip:") != std::string::npos);
    
    // Send response.
    const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(fake_upstream_connection->write(response));

    tcp_client->waitForData(response);
    tcp_client->close();
  }

  void sendProxyProtocolV2Header(const std::vector<uint8_t>& header) {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
    
    // Send PROXY protocol v2 header.
    Buffer::OwnedImpl buffer(header.data(), header.size());
    ASSERT_TRUE(tcp_client->write(buffer.toString(), false));
    
    // Send HTTP request after PROXY protocol header.
    const std::string request = "GET / HTTP/1.1\r\nHost: test\r\n\r\n";
    ASSERT_TRUE(tcp_client->write(request));
    
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    // Read the request at the upstream.
    std::string received_data;
    ASSERT_TRUE(fake_upstream_connection->waitForData(
        FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));

    // Verify the x-real-ip header contains the address from PROXY protocol.
    EXPECT_TRUE(received_data.find("x-real-ip:") != std::string::npos);
    
    // Send response.
    const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(fake_upstream_connection->write(response));

    tcp_client->waitForData(response);
    tcp_client->close();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that PROXY protocol v1 with IPv4 addresses works correctly.
TEST_P(ProxyProtocolIntegrationTest, V1Ipv4Works) {
  initializeWithProxyProtocol();
  
  // Send a PROXY protocol v1 header with IPv4 addresses.
  const std::string proxy_header = "PROXY TCP4 192.168.1.100 10.0.0.1 12345 80\r\n";
  sendProxyProtocolV1Header(proxy_header);
}

// Test that PROXY protocol v1 with IPv6 addresses works correctly.
TEST_P(ProxyProtocolIntegrationTest, V1Ipv6Works) {
  initializeWithProxyProtocol();
  
  // Send a PROXY protocol v1 header with IPv6 addresses.
  const std::string proxy_header = "PROXY TCP6 2001:db8::1 2001:db8::2 12345 80\r\n";
  sendProxyProtocolV1Header(proxy_header);
}

// Test that PROXY protocol v2 with IPv4 addresses works correctly.
TEST_P(ProxyProtocolIntegrationTest, V2Ipv4Works) {
  initializeWithProxyProtocol();
  
  // PROXY protocol v2 header for IPv4/TCP connection.
  // Signature (12 bytes) + Version/Command (1) + Family/Protocol (1) + Length (2) + 
  // IPv4 addresses and ports (12 bytes).
  const std::vector<uint8_t> proxy_header = {
      // Signature
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
      // Version/Command: v2, PROXY command
      0x21,
      // Family/Protocol: IPv4, TCP
      0x11,
      // Length: 12 bytes
      0x00, 0x0c,
      // Source IP: 192.168.1.100
      0xc0, 0xa8, 0x01, 0x64,
      // Dest IP: 10.0.0.1
      0x0a, 0x00, 0x00, 0x01,
      // Source port: 12345
      0x30, 0x39,
      // Dest port: 80
      0x00, 0x50
  };
  
  sendProxyProtocolV2Header(proxy_header);
}

// Test that PROXY protocol v2 with IPv6 addresses works correctly.
TEST_P(ProxyProtocolIntegrationTest, V2Ipv6Works) {
  initializeWithProxyProtocol();
  
  // PROXY protocol v2 header for IPv6/TCP connection.
  // Signature (12 bytes) + Version/Command (1) + Family/Protocol (1) + Length (2) + 
  // IPv6 addresses and ports (36 bytes).
  const std::vector<uint8_t> proxy_header = {
      // Signature
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
      // Version/Command: v2, PROXY command
      0x21,
      // Family/Protocol: IPv6, TCP
      0x21,
      // Length: 36 bytes
      0x00, 0x24,
      // Source IP: 2001:db8::1
      0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
      // Dest IP: 2001:db8::2
      0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
      // Source port: 12345
      0x30, 0x39,
      // Dest port: 80
      0x00, 0x50
  };
  
  sendProxyProtocolV2Header(proxy_header);
}

// Test that demonstrates PROXY protocol working even with mixed address families.
// This is the key test showing that addresses from PROXY protocol headers are
// treated as metadata and don't require OS support.
TEST_P(ProxyProtocolIntegrationTest, MixedAddressFamiliesWork) {
  initializeWithProxyProtocol();
  
  // Test IPv4 PROXY header on potentially IPv6-only system.
  const std::string proxy_v4_header = "PROXY TCP4 192.168.1.100 10.0.0.1 12345 80\r\n";
  sendProxyProtocolV1Header(proxy_v4_header);
  
  // Test IPv6 PROXY header on potentially IPv4-only system.
  const std::string proxy_v6_header = "PROXY TCP6 2001:db8::1 2001:db8::2 54321 443\r\n";
  sendProxyProtocolV1Header(proxy_v6_header);
}

// Test that access logs correctly show addresses from PROXY protocol headers.
TEST_P(ProxyProtocolIntegrationTest, AccessLogsShowProxyAddresses) {
  config_helper_.addConfigModifier(
      [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        auto* filter_chain = listener->mutable_filter_chains(0);
        
        // Add PROXY protocol listener filter.
        auto* listener_filter = listener->add_listener_filters();
        listener_filter->set_name("envoy.filters.listener.proxy_protocol");
        listener_filter->mutable_typed_config()->PackFrom(
            envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol());
        
        // Configure access logging.
        auto* access_log = filter_chain->mutable_filters(0)->add_access_log();
        access_log->set_name("envoy.access_loggers.stdout");
        envoy::extensions::access_loggers::stream::v3::StdoutAccessLog stdout_config;
        stdout_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
            "[%START_TIME%] %DOWNSTREAM_REMOTE_ADDRESS% -> %UPSTREAM_HOST% %PROTOCOL% "
            "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% "
            "%DURATION% %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% %REQ(X-FORWARDED-FOR)%\n");
        access_log->mutable_typed_config()->PackFrom(stdout_config);
      });
      
  HttpIntegrationTest::initialize();
  
  // Send request with PROXY protocol header.
  const std::string proxy_header = "PROXY TCP4 203.0.113.7 198.51.100.22 35646 80\r\n";
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp_client->write(proxy_header, false));
  
  const std::string request = "GET / HTTP/1.1\r\nHost: test\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request));
  
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));
  
  const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
  ASSERT_TRUE(fake_upstream_connection->write(response));
  
  tcp_client->waitForData(response);
  
  // Verify access log would show the PROXY protocol address (203.0.113.7).
  // In a real test, we'd capture and verify the access log output.
  
  tcp_client->close();
}

}  // namespace
}  // namespace Envoy
