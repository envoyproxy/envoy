#include "ssl_integration_test.h"

#include <memory>
#include <string>

#include "common/event/dispatcher_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

namespace Envoy {
using testing::Return;

namespace Ssl {

std::unique_ptr<Runtime::Loader> SslIntegrationTest::runtime_;
std::unique_ptr<ContextManager> SslIntegrationTest::context_manager_;
ServerContextPtr SslIntegrationTest::upstream_ssl_ctx_;
ClientContextPtr SslIntegrationTest::client_ssl_ctx_plain_;
ClientContextPtr SslIntegrationTest::client_ssl_ctx_alpn_;
ClientContextPtr SslIntegrationTest::client_ssl_ctx_san_;
ClientContextPtr SslIntegrationTest::client_ssl_ctx_alpn_san_;

void SslIntegrationTest::SetUpTestCase() {
  runtime_.reset(new NiceMock<Runtime::MockLoader>());
  context_manager_.reset(new ContextManagerImpl(*runtime_));
  upstream_ssl_ctx_ = createUpstreamSslContext();
  fake_upstreams_.emplace_back(
      new FakeUpstream(upstream_ssl_ctx_.get(), 0, FakeHttpConnection::Type::HTTP1));
  registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
  fake_upstreams_.emplace_back(
      new FakeUpstream(upstream_ssl_ctx_.get(), 0, FakeHttpConnection::Type::HTTP1));
  registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
  test_server_ = MockRuntimeIntegrationTestServer::create(TestEnvironment::temporaryFileSubstitute(
      "test/config/integration/server_ssl.json", port_map()));
  registerTestServerPorts({"http"});
  client_ssl_ctx_plain_ = createClientSslContext(false, false);
  client_ssl_ctx_alpn_ = createClientSslContext(true, false);
  client_ssl_ctx_san_ = createClientSslContext(false, true);
  client_ssl_ctx_alpn_san_ = createClientSslContext(true, true);
}

void SslIntegrationTest::TearDownTestCase() {
  test_server_.reset();
  fake_upstreams_.clear();
  upstream_ssl_ctx_.reset();
  client_ssl_ctx_plain_.reset();
  client_ssl_ctx_alpn_.reset();
  client_ssl_ctx_san_.reset();
  client_ssl_ctx_alpn_san_.reset();
  context_manager_.reset();
  runtime_.reset();
}

ServerContextPtr SslIntegrationTest::createUpstreamSslContext() {
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  std::string json = R"EOF(
{
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/upstreamcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/upstreamkey.pem"
}
)EOF";

  Json::ObjectPtr loader = TestEnvironment::jsonLoadFromString(json);
  ContextConfigImpl cfg(*loader);
  return context_manager_->createSslServerContext(*upstream_stats_store, cfg);
}

ClientContextPtr SslIntegrationTest::createClientSslContext(bool alpn, bool san) {
  std::string json_plain = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem"
}
)EOF";

  std::string json_alpn = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "alpn_protocols": "h2,http/1.1"
}
)EOF";

  std::string json_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "verify_subject_alt_name": [ "istio:account_a.namespace_foo.cluster.local" ]
}
)EOF";

  std::string json_alpn_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "alpn_protocols": "h2,http/1.1",
  "verify_subject_alt_name": [ "istio:account_a.namespace_foo.cluster.local" ]
}
)EOF";

  std::string target;
  if (alpn) {
    target = san ? json_alpn_san : json_alpn;
  } else {
    target = san ? json_san : json_plain;
  }
  Json::ObjectPtr loader = TestEnvironment::jsonLoadFromString(target);
  ContextConfigImpl cfg(*loader);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return context_manager_->createSslClientContext(*client_stats_store, cfg);
}

Network::ClientConnectionPtr SslIntegrationTest::makeSslClientConnection(bool alpn, bool san) {
  if (alpn) {
    return dispatcher_->createSslClientConnection(
        san ? *client_ssl_ctx_alpn_san_ : *client_ssl_ctx_alpn_,
        Network::Utility::resolveUrl("tcp://127.0.0.1:" + std::to_string(lookupPort("http"))));
  } else {
    return dispatcher_->createSslClientConnection(
        san ? *client_ssl_ctx_san_ : *client_ssl_ctx_plain_,
        Network::Utility::resolveUrl("tcp://127.0.0.1:" + std::to_string(lookupPort("http"))));
  }
}

void SslIntegrationTest::checkStats() {
  Stats::Counter& counter = test_server_->store().counter("listener.127.0.0.1_0.ssl.handshake");
  EXPECT_EQ(1U, counter.value());
  counter.reset();
}

TEST_F(SslIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeSslClientConnection(false, false),
                                       Http::CodecClient::Type::HTTP1, 16 * 1024 * 1024,
                                       16 * 1024 * 1024, false);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeSslClientConnection(false, false),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2) {
  testRouterRequestAndResponseWithBody(makeSslClientConnection(true, false),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, false);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferVierfySAN) {
  testRouterRequestAndResponseWithBody(makeSslClientConnection(false, true),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2VerifySAN) {
  testRouterRequestAndResponseWithBody(makeSslClientConnection(true, true),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, false);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  testRouterHeaderOnlyRequestAndResponse(makeSslClientConnection(false, false),
                                         Http::CodecClient::Type::HTTP1);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(makeSslClientConnection(false, false),
                                                     Http::CodecClient::Type::HTTP1);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(makeSslClientConnection(false, false),
                                                      Http::CodecClient::Type::HTTP1);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(makeSslClientConnection(false, false),
                                                       Http::CodecClient::Type::HTTP1);
  checkStats();
}

// This test must be here vs integration_admin_test so that it tests a server with loaded certs.
TEST_F(SslIntegrationTest, AdminCertEndpoint) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/certs", "", Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_F(SslIntegrationTest, AltAlpn) {
  // Connect with ALPN, but we should end up using HTTP/1.
  MockRuntimeIntegrationTestServer* server =
      dynamic_cast<MockRuntimeIntegrationTestServer*>(test_server_.get());
  ON_CALL(server->runtime_->snapshot_, featureEnabled("ssl.alt_alpn", 0))
      .WillByDefault(Return(true));
  testRouterRequestAndResponseWithBody(makeSslClientConnection(true, false),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
  checkStats();
}

} // Ssl
} // Envoy
