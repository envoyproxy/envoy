#pragma once

#include <memory>
#include <string>

#include "test/integration/integration.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {
class TcpProxyIntegrationTest : public BaseIntegrationTest,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  TcpProxyIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::TCP_PROXY_CONFIG) {
    enable_half_close_ = true;
  }

  void initialize() override;

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

class TcpProxySslIntegrationTest : public TcpProxyIntegrationTest {
public:
  void initialize() override;
  void setupConnections();
  void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                             const std::string& data_to_send_downstream);

  Network::ClientConnectionPtr ssl_client_;
  FakeRawConnectionPtr fake_upstream_connection_;
  testing::NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  MockWatermarkBuffer* client_write_buffer_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
};

} // namespace
} // namespace Envoy
