#include "xfcc_integration_test.h"

#include <regex>

#include "common/event/dispatcher_impl.h"
#include "common/http/header_map_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "ssl_integration_test.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

namespace Envoy {

namespace Xfcc {

void XfccIntegrationTest::SetUp() {
  runtime_.reset(new NiceMock<Runtime::MockLoader>());
  context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_));
  upstream_ssl_ctx_ = createUpstreamSslContext();
  fake_upstreams_.emplace_back(
      new FakeUpstream(upstream_ssl_ctx_.get(), 0, FakeHttpConnection::Type::HTTP1, version_));
  registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
  fake_upstreams_.emplace_back(
      new FakeUpstream(upstream_ssl_ctx_.get(), 0, FakeHttpConnection::Type::HTTP1, version_));
  registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
  client_ssl_ctx_ = createClientSslContext();
}

void XfccIntegrationTest::TearDown() {
  test_server_.reset();
  client_ssl_ctx_.reset();
  fake_upstreams_.clear();
  upstream_ssl_ctx_.reset();
  context_manager_.reset();
  runtime_.reset();
}

Ssl::ClientContextPtr XfccIntegrationTest::createClientSslContext() {
  std::string json_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "verify_subject_alt_name": [ "spiffe://lyft.com/backend-team" ]
}
)EOF";
 
  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json_san);
  Ssl::ContextConfigImpl cfg(*loader);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return context_manager_->createSslClientContext(*client_stats_store, cfg);
}

Ssl::ServerContextPtr XfccIntegrationTest::createUpstreamSslContext() {
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  std::string json = R"EOF(
{
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/upstreamcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/upstreamkey.pem"
}
)EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  Ssl::ContextConfigImpl cfg(*loader);
  return context_manager_->createSslServerContext(*upstream_stats_store, cfg);
}

Network::ClientConnectionPtr XfccIntegrationTest::makeSslClientConnection() {
  Network::Address::InstanceConstSharedPtr address =
    Network::Utility::resolveUrl("tcp://" + Network::Test::getLoopbackAddressUrlString(version_) +
        ":" + std::to_string(lookupPort("http")));
  return dispatcher_->createSslClientConnection(*client_ssl_ctx_, address);
}

void XfccIntegrationTest::testRequestAndResponseWithXfccHeader(
    Network::ClientConnectionPtr&& conn, std::string expected_xfcc) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request;
  executeActions(
      {[&]() -> void {
         codec_client = makeHttpConnection(std::move(conn), Http::CodecClient::Type::HTTP1);
       },
       [&]() -> void {
         codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                     {":path", "/test/long/url"},
                                                                     {":scheme", "http"},
                                                                     {":authority", "host"},
                                                                     {"x-forwarded-client-cert",
                                                                      previous_xfcc_.c_str()}},
                                             *response);
       },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_); },
       [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { upstream_request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         if (expected_xfcc.empty()) {
           EXPECT_EQ(nullptr, upstream_request->headers().ForwardedClientCert());
         } else {
           EXPECT_STREQ(expected_xfcc.c_str(), upstream_request->headers().ForwardedClientCert()->value().c_str());
        }
         upstream_request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(upstream_request->complete());
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
  EXPECT_TRUE(response->complete());
}

void XfccIntegrationTest::startTestServerWithXfccConfig(std::string fcc, std::string sccd) {
  TestEnvironment::ParamMap param_map;
  param_map["forward_client_cert"] = fcc;
  param_map["set_client_cert_details"] = sccd;
  std::string config = TestEnvironment::temporaryFileSubstitute(
      "test/config/integration/server_xfcc.json", param_map, port_map_, version_);
  test_server_ = Ssl::MockRuntimeIntegrationTestServer::create(config, version_);
  registerTestServerPorts({"http"});
}

INSTANTIATE_TEST_CASE_P(IpVersions, XfccIntegrationTest,
    testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(XfccIntegrationTest, ForwardOnly) {
  startTestServerWithXfccConfig("forward_only", "\"SAN\"");
  testRequestAndResponseWithXfccHeader(makeSslClientConnection(), previous_xfcc_);
}

TEST_P(XfccIntegrationTest, Sanitize) {
  startTestServerWithXfccConfig("sanitize", "");
  testRequestAndResponseWithXfccHeader(makeSslClientConnection(), "");
}

TEST_P(XfccIntegrationTest, AppendForward) {
  startTestServerWithXfccConfig("append_forward", "\"SAN\"");
  testRequestAndResponseWithXfccHeader(makeSslClientConnection(),
                                       previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" + client_san_);
}
} // Xfcc
} // Envoy
