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

class BaseTcpProxyIntegrationTest : public BaseIntegrationTest {
public:
  BaseTcpProxyIntegrationTest(Network::Address::IpVersion version)
      : BaseIntegrationTest(version, ConfigHelper::tcpProxyConfig()) {
    enableHalfClose(true);
  }

  void initialize() override;

  // Setup common byte metering parameters.
  void setupByteMeterAccessLog();
};

class TcpProxyIntegrationTest : public BaseTcpProxyIntegrationTest,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  TcpProxyIntegrationTest() : BaseTcpProxyIntegrationTest(GetParam()) {}
};

/**
 * An interface to exercise a client connection with either TLS or plaintext.
 */
class TestClientConnection {
public:
  virtual ~TestClientConnection() = default;
  virtual void waitForUpstreamConnection() PURE;
  virtual void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                                     const std::string& data_to_send_downstream) PURE;
  virtual void close() PURE;
  virtual void waitForDisconnect() PURE;
};

class BaseTcpProxySslIntegrationTest : public BaseTcpProxyIntegrationTest {
public:
  BaseTcpProxySslIntegrationTest(Network::Address::IpVersion version)
      : BaseTcpProxyIntegrationTest(version) {}
  void initialize() override;

  void setupConnections();
  void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                             const std::string& data_to_send_downstream);
  virtual FakeUpstream* dataStream() { return fake_upstreams_.front().get(); }

protected:
  struct ClientSslConnection : public TestClientConnection {
    ClientSslConnection(BaseTcpProxySslIntegrationTest& parent);
    void waitForUpstreamConnection() override;
    void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                               const std::string& data_to_send_downstream) override;
    void close() override;
    void waitForDisconnect() override;
    BaseTcpProxySslIntegrationTest& parent_;
    ConnectionStatusCallbacks connect_callbacks_;
    MockWatermarkBuffer* client_write_buffer_;
    std::shared_ptr<WaitForPayloadReader> payload_reader_;
    Network::ClientConnectionPtr ssl_client_;
    FakeRawConnectionPtr fake_upstream_connection_;
  };

  struct ClientRawConnection : public TestClientConnection {
    ClientRawConnection(BaseTcpProxySslIntegrationTest& parent)
        : parent_(parent), tcp_client_(*parent.dispatcher_, *parent.mock_buffer_factory_,
                                       parent.lookupPort("tcp_proxy"), parent.version_,
                                       parent.enableHalfClose(), nullptr) {}
    void close() override;
    void waitForUpstreamConnection() override;
    void sendAndReceiveTlsData(const std::string& data_to_send_upstream,
                               const std::string& data_to_send_downstream) override;
    void waitForDisconnect() override;
    BaseTcpProxySslIntegrationTest& parent_;
    IntegrationTcpClient tcp_client_;
    FakeRawConnectionPtr fake_upstream_connection_;
  };

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Ssl::ClientSslTransportOptions ssl_options_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  std::unique_ptr<ClientSslConnection> client_;
};

class TcpProxySslIntegrationTest : public BaseTcpProxySslIntegrationTest,
                                   public testing::TestWithParam<Network::Address::IpVersion> {
public:
  TcpProxySslIntegrationTest() : BaseTcpProxySslIntegrationTest(GetParam()) {}
};

} // namespace Envoy
