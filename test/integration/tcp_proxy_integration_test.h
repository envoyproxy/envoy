#pragma once

#include <memory>
#include <string>

#include "test/integration/integration.h"
#include "test/mocks/secret/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {

struct TcpProxyIntegrationTestParams {
  Network::Address::IpVersion version;
  bool test_original_version;
};

class TcpProxyIntegrationTest : public testing::TestWithParam<TcpProxyIntegrationTestParams>,
                                public BaseIntegrationTest {
public:
  TcpProxyIntegrationTest()
      : BaseIntegrationTest(GetParam().version, ConfigHelper::tcpProxyConfig()) {
    enableHalfClose(true);
  }

  void initialize() override;
};

class InternalTcpProxyIntegrationTest
    : public testing::TestWithParam<TcpProxyIntegrationTestParams>,
      public BaseIntegrationTest {
public:
  InternalTcpProxyIntegrationTest()
      : BaseIntegrationTest(GetParam().version, internalTcpProxyConfig()) {
    enableHalfClose(true);
  }
  static std::string internalTcpProxyConfig() {
    return absl::StrCat(fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  lds_config:
    resource_api_version: V3
    path: {}
static_resources:
  secrets:
  - name: "secret_static_0"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES"
      private_key:
        inline_string: "DUMMY_INLINE_BYTES"
      password:
        inline_string: "DUMMY_INLINE_BYTES"
  clusters:
    name: cluster_0
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  listeners:
    name: listener_0
    address:
      envoy_internal_address:
        server_listener_name: test_internal_listener_foo
    internal_listener: {{  }} # escape the bracket
)EOF",
                                    Platform::null_device_path, Platform::null_device_path),
                        R"EOF(
    filter_chains:
      filters:
        name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF");
  }

  void initialize() override;
};

class TcpProxySslIntegrationTest : public TcpProxyIntegrationTest {
public:
  void initialize() override;
  void setupConnections();
  void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                             const std::string& data_to_send_downstream);

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  MockWatermarkBuffer* client_write_buffer_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::ClientConnectionPtr ssl_client_;
  FakeRawConnectionPtr fake_upstream_connection_;
};

} // namespace Envoy
