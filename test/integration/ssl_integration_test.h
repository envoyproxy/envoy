#pragma once

#include <memory>
#include <string>

#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Ssl {

class MockRuntimeIntegrationTestServer : public IntegrationTestServer {
public:
  static IntegrationTestServerPtr create(const std::string& config_path,
                                         Network::Address::IpVersion version) {
    IntegrationTestServerPtr server{new MockRuntimeIntegrationTestServer(config_path)};
    server->start(version);
    return server;
  }

  // Server::ComponentFactory
  Runtime::LoaderPtr createRuntime(Server::Instance&, Server::Configuration::Initial&) override {
    runtime_ = new NiceMock<Runtime::MockLoader>();
    return Runtime::LoaderPtr{runtime_};
  }

  Runtime::MockLoader* runtime_;

private:
  MockRuntimeIntegrationTestServer(const std::string& config_path)
      : IntegrationTestServer(config_path, std::string()) {}
};

class SslIntegrationTest : public BaseIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SslIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Initializer for an individual test.
   */
  void SetUp() override;

  /**
   * Destructor for an individual test.
   */
  void TearDown() override;

  Network::ClientConnectionPtr makeSslClientConnection(bool alpn, bool san);
  ServerContextPtr createUpstreamSslContext();
  void checkStats();

private:
  std::unique_ptr<Runtime::Loader> runtime_;
  std::unique_ptr<ContextManager> context_manager_;
  ServerContextPtr upstream_ssl_ctx_;
  ClientContextPtr client_ssl_ctx_plain_;
  ClientContextPtr client_ssl_ctx_alpn_;
  ClientContextPtr client_ssl_ctx_san_;
  ClientContextPtr client_ssl_ctx_alpn_san_;
};

} // namespace Ssl
} // namespace Envoy
