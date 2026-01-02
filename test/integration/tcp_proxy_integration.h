#pragma once

#include <memory>
#include <string>

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/secret/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {

struct TcpProxyIntegrationTestParams {
  Network::Address::IpVersion version;
  bool test_original_version;
};

class TcpProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public BaseIntegrationTest {
public:
  TcpProxyIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    enableHalfClose(true);
  }

  void initialize() override;
  // Setup common byte metering parameters.
  void setupByteMeterAccessLog();
};

class TcpProxySslIntegrationTest : public TcpProxyIntegrationTest {
public:
  void initialize() override;

  struct ClientSslConnection {
    ClientSslConnection(TcpProxySslIntegrationTest& parent);
    void waitForUpstreamConnection();
    void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                               const std::string& data_to_send_downstream);
    TcpProxySslIntegrationTest& parent_;
    ConnectionStatusCallbacks connect_callbacks_;
    MockWatermarkBuffer* client_write_buffer_;
    std::shared_ptr<WaitForPayloadReader> payload_reader_;
    Network::ClientConnectionPtr ssl_client_;
    FakeRawConnectionPtr fake_upstream_connection_;
  };

  void setupConnections();
  void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                             const std::string& data_to_send_downstream);
  virtual FakeUpstream* dataStream() { return fake_upstreams_.front().get(); }

protected:
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Ssl::ClientSslTransportOptions ssl_options_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  std::unique_ptr<ClientSslConnection> client_;
};

} // namespace Envoy
