#pragma once

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/server.h"

#include "common/http/codec_client.h"
#include "common/stats/stats_impl.h"
#include "common/ssl/openssl.h"

#include "test/mocks/runtime/mocks.h"

using testing::NiceMock;

namespace Ssl {

class TestServerContextImpl : public ContextImpl, public ServerContext {
public:
  TestServerContextImpl(const std::string& name, Stats::Store& stats, ContextConfig& config)
      : ContextImpl(name, stats, config) {}
};

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
  static void SetUpTestCase() {
    test_server_ =
        MockRuntimeIntegrationTestServer::create("test/config/integration/server_ssl.json");
    upstream_ssl_ctx_ = createUpstreamSslContext("upstream", store());
    client_ssl_ctx_alpn_ = createClientSslContext("client", store(), true);
    client_ssl_ctx_no_alpn_ = createClientSslContext("client", store(), false);
    fake_upstreams_.emplace_back(
        new FakeUpstream(upstream_ssl_ctx_.get(), 11000, FakeHttpConnection::Type::HTTP1));
    fake_upstreams_.emplace_back(
        new FakeUpstream(upstream_ssl_ctx_.get(), 11001, FakeHttpConnection::Type::HTTP1));
  }

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase() {
    test_server_.reset();
    fake_upstreams_.clear();
    upstream_ssl_ctx_.reset();
    client_ssl_ctx_alpn_.reset();
    client_ssl_ctx_no_alpn_.reset();
  }

  Network::ClientConnectionPtr makeSslClientConnection(bool alpn);

  static ServerContextPtr createUpstreamSslContext(const std::string& name, Stats::Store& store);
  static ClientContextPtr createClientSslContext(const std::string& name, Stats::Store& store,
                                                 bool alpn);

  static Stats::Store& store() { return test_server_->server().stats(); }

  void checkStats();

private:
  static ServerContextPtr upstream_ssl_ctx_;
  static ClientContextPtr client_ssl_ctx_alpn_;
  static ClientContextPtr client_ssl_ctx_no_alpn_;
};

} // Ssl
