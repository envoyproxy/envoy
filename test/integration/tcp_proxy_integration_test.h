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
  void setupConnections();
  void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                             const std::string& data_to_send_downstream);

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  MockWatermarkBuffer* client_write_buffer_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::ClientConnectionPtr ssl_client_;
  FakeRawConnectionPtr fake_upstream_connection_;
};

} // namespace Envoy
