#include "integration.h"
#include "ssl_integration_test.h"
#include "utility.h"

#include "common/event/dispatcher_impl.h"

using testing::Return;

namespace Ssl {

ServerContextPtr SslIntegrationTest::upstream_ssl_ctx_;
ClientContextPtr SslIntegrationTest::client_ssl_ctx_alpn_;
ClientContextPtr SslIntegrationTest::client_ssl_ctx_no_alpn_;

ServerContextPtr SslIntegrationTest::createUpstreamSslContext(const std::string& name,
                                                              Stats::Store& store) {
  std::string json = R"EOF(
{
  "cert_chain_file": "test/config/integration/certs/upstreamcert.pem",
  "private_key_file": "test/config/integration/certs/upstreamkey.pem"
}
)EOF";

  Json::StringLoader loader(json);
  ContextConfigImpl cfg(loader);
  return ServerContextPtr(new TestServerContextImpl(name, store, cfg));
}

ClientContextPtr SslIntegrationTest::createClientSslContext(const std::string& name,
                                                            Stats::Store& store, bool alpn) {
  std::string json_no_alpn = R"EOF(
{
  "ca_cert_file": "test/config/integration/certs/cacert.pem",
  "cert_chain_file": "test/config/integration/certs/clientcert.pem",
  "private_key_file": "test/config/integration/certs/clientkey.pem"
}
)EOF";

  std::string json_alpn = R"EOF(
{
  "ca_cert_file": "test/config/integration/certs/cacert.pem",
  "cert_chain_file": "test/config/integration/certs/clientcert.pem",
  "private_key_file": "test/config/integration/certs/clientkey.pem",
  "alpn_protocols": "h2,http/1.1"
}
)EOF";

  Json::StringLoader loader(alpn ? json_alpn : json_no_alpn);
  ContextConfigImpl cfg(loader);
  return ClientContextPtr(new ClientContextImpl(name, store, cfg));
}

Network::ClientConnectionPtr SslIntegrationTest::makeSslClientConnection(bool alpn) {
  return dispatcher_->createSslClientConnection(alpn ? *client_ssl_ctx_alpn_
                                                     : *client_ssl_ctx_no_alpn_,
                                                fmt::format("tcp://127.0.0.1:10001"));
}

void SslIntegrationTest::checkStats() {
  Stats::Counter& counter = store().counter("listener.10001.ssl.handshake");
  EXPECT_EQ(1U, counter.value());
  counter.reset();
}

TEST_F(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeSslClientConnection(false),
                                       Http::CodecClient::Type::HTTP1, 1024, 512);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2) {
  testRouterRequestAndResponseWithBody(makeSslClientConnection(true),
                                       Http::CodecClient::Type::HTTP2, 1024, 512);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  testRouterHeaderOnlyRequestAndResponse(makeSslClientConnection(false),
                                         Http::CodecClient::Type::HTTP1);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(makeSslClientConnection(false),
                                                     Http::CodecClient::Type::HTTP1);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(makeSslClientConnection(false),
                                                      Http::CodecClient::Type::HTTP1);
  checkStats();
}

TEST_F(SslIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(makeSslClientConnection(false),
                                                       Http::CodecClient::Type::HTTP1);
  checkStats();
}

// This test must be here vs integration_admin_test so that it tests a server with loaded certs.
TEST_F(SslIntegrationTest, AdminCertEndpoint) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      ADMIN_PORT, "GET", "/certs", "", Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().get(":status"));
}

TEST_F(SslIntegrationTest, AltAlpn) {
  // Connect with ALPN, but we should end up using HTTP/1.
  MockRuntimeIntegrationTestServer* server =
      dynamic_cast<MockRuntimeIntegrationTestServer*>(test_server_.get());
  ON_CALL(server->runtime_->snapshot_, featureEnabled("ssl.alt_alpn", 0))
      .WillByDefault(Return(true));
  testRouterRequestAndResponseWithBody(makeSslClientConnection(true),
                                       Http::CodecClient::Type::HTTP1, 1024, 512);
  checkStats();
}

} // Ssl
