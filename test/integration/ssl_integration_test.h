#pragma once

#include "test/integration/integration.h"
#include "test/integration/server.h"

#include "test/mocks/runtime/mocks.h"

using testing::NiceMock;

namespace Ssl {

class MockRuntimeIntegrationTestServer : public IntegrationTestServer {
public:
  static IntegrationTestServerPtr create(const std::string& config_path) {
    IntegrationTestServerPtr server{new MockRuntimeIntegrationTestServer(config_path)};
    server->start();
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
      : IntegrationTestServer(config_path) {}
};

class SslIntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  static void SetUpTestCase();

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase();

  Network::ClientConnectionPtr makeSslClientConnection(bool alpn, bool san);
  static ServerContextPtr createUpstreamSslContext();
  static ClientContextPtr createClientSslContext(bool alpn, bool san);
  void checkStats();

private:
  static std::unique_ptr<Runtime::Loader> runtime_;
  static std::unique_ptr<ContextManager> context_manager_;
  static ServerContextPtr upstream_ssl_ctx_;
  static ClientContextPtr client_ssl_ctx_plain_;
  static ClientContextPtr client_ssl_ctx_alpn_;
  static ClientContextPtr client_ssl_ctx_san_;
  static ClientContextPtr client_ssl_ctx_alpn_san_;
};

} // Ssl
