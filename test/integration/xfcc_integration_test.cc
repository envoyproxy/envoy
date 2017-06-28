#include "xfcc_integration_test.h"

#include <regex>

#include "common/event/dispatcher_impl.h"
#include "common/http/header_map_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "ssl_integration_test.h"
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
  client_tls_ssl_ctx_ = createClientSslContext(false);
  client_mtls_ssl_ctx_ = createClientSslContext(true);
}

void XfccIntegrationTest::TearDown() {
  test_server_.reset();
  client_mtls_ssl_ctx_.reset();
  client_tls_ssl_ctx_.reset();
  fake_upstreams_.clear();
  upstream_ssl_ctx_.reset();
  context_manager_.reset();
  runtime_.reset();
}

Ssl::ClientContextPtr XfccIntegrationTest::createClientSslContext(bool mtls) {
  std::string json_tls = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "verify_subject_alt_name": [ "spiffe://lyft.com/backend-team" ]
}
)EOF";
  std::string json_mtls = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "verify_subject_alt_name": [ "spiffe://lyft.com/backend-team" ]
}
)EOF";

  std::string target;
  if (mtls) {
    target = json_mtls;
  } else {
    target = json_tls;
  }
  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(target);
  Ssl::ContextConfigImpl cfg(*loader);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return context_manager_->createSslClientContext(*client_stats_store, cfg);
}

Ssl::ServerContextPtr XfccIntegrationTest::createUpstreamSslContext() {
  std::string json = R"EOF(
{
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/upstreamcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/upstreamkey.pem"
}
)EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  Ssl::ContextConfigImpl cfg(*loader);
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return context_manager_->createSslServerContext(*upstream_stats_store, cfg);
}

Network::ClientConnectionPtr XfccIntegrationTest::makeClientConnection() {
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://" + Network::Test::getLoopbackAddressUrlString(version_) +
                                   ":" + std::to_string(lookupPort("plain")));
  return dispatcher_->createClientConnection(address);
}

Network::ClientConnectionPtr XfccIntegrationTest::makeTlsClientConnection() {
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://" + Network::Test::getLoopbackAddressUrlString(version_) +
                                   ":" + std::to_string(lookupPort("ssl")));
  return dispatcher_->createSslClientConnection(*client_tls_ssl_ctx_, address);
}

Network::ClientConnectionPtr XfccIntegrationTest::makeMtlsClientConnection() {
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://" + Network::Test::getLoopbackAddressUrlString(version_) +
                                   ":" + std::to_string(lookupPort("ssl")));
  return dispatcher_->createSslClientConnection(*client_mtls_ssl_ctx_, address);
}

void XfccIntegrationTest::startTestServerWithXfccConfig(std::string fcc, std::string sccd) {
  TestEnvironment::ParamMap param_map;
  param_map["forward_client_cert"] = fcc;
  param_map["set_current_client_cert_details"] = sccd;
  std::string config = TestEnvironment::temporaryFileSubstitute(
      "test/config/integration/server_xfcc.json", param_map, port_map_, version_);
  test_server_ = Ssl::MockRuntimeIntegrationTestServer::create(config, version_);
  registerTestServerPorts({"ssl", "plain"});
}

void XfccIntegrationTest::testRequestAndResponseWithXfccHeader(Network::ClientConnectionPtr&& conn,
                                                               std::string previous_xfcc,
                                                               std::string expected_xfcc) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request;
  Http::TestHeaderMapImpl header_map;
  if (previous_xfcc.empty()) {
    header_map = Http::TestHeaderMapImpl{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"}};
  } else {
    header_map = Http::TestHeaderMapImpl{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"x-forwarded-client-cert", previous_xfcc.c_str()}};
  }

  executeActions(
      {[&]() -> void {
        codec_client = makeHttpConnection(std::move(conn), Http::CodecClient::Type::HTTP1);
      },
       [&]() -> void { codec_client->makeHeaderOnlyRequest(header_map, *response); },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { upstream_request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         if (expected_xfcc.empty()) {
           EXPECT_EQ(nullptr, upstream_request->headers().ForwardedClientCert());
         } else {
           EXPECT_STREQ(expected_xfcc.c_str(),
                        upstream_request->headers().ForwardedClientCert()->value().c_str());
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

INSTANTIATE_TEST_CASE_P(IpVersions, XfccIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(XfccIntegrationTest, MtlsForwardOnly) {
  startTestServerWithXfccConfig("forward_only", "");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, MtlsAlwaysForwardOnly) {
  startTestServerWithXfccConfig("always_forward_only", "");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, MtlsSanitize) {
  startTestServerWithXfccConfig("sanitize", "");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_, "");
}

TEST_P(XfccIntegrationTest, MtlsSanitizeSetSubjectSan) {
  startTestServerWithXfccConfig("sanitize_set", "\"Subject\", \"SAN\"");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_,
                                       current_xfcc_by_hash_ + ";" + client_subject_ + ";" +
                                           client_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForward) {
  startTestServerWithXfccConfig("append_forward", "");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_,
                                       previous_xfcc_ + "," + current_xfcc_by_hash_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSubject) {
  startTestServerWithXfccConfig("append_forward", "\"Subject\"");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_,
                                       previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" +
                                           client_subject_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSan) {
  startTestServerWithXfccConfig("append_forward", "\"SAN\"");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_,
                                       previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" +
                                           client_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSubjectSan) {
  startTestServerWithXfccConfig("append_forward", "\"Subject\", \"SAN\"");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), previous_xfcc_,
                                       previous_xfcc_ + "," + current_xfcc_by_hash_ + ";" +
                                           client_subject_ + ";" + client_san_);
}

TEST_P(XfccIntegrationTest, MtlsAppendForwardSanPreviousXfccHeaderEmpty) {
  startTestServerWithXfccConfig("append_forward", "\"SAN\"");
  testRequestAndResponseWithXfccHeader(makeMtlsClientConnection(), "",
                                       current_xfcc_by_hash_ + ";" + client_san_);
}

TEST_P(XfccIntegrationTest, TlsAlwaysForwardOnly) {
  // The always_forward_only works regardless of whether the connection is TLS/mTLS.
  startTestServerWithXfccConfig("always_forward_only", "");
  testRequestAndResponseWithXfccHeader(makeClientConnection(), previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, TlsEnforceSanitize) {
  // The forward_only, append_forward and sanitize_set options are not effective when the connection
  // is not using Mtls.
  startTestServerWithXfccConfig("forward_only", "");
  testRequestAndResponseWithXfccHeader(makeClientConnection(), previous_xfcc_, "");
}

TEST_P(XfccIntegrationTest, NonTlsAlwaysForwardOnly) {
  // The always_forward_only works regardless of whether the connection is TLS/mTLS.
  startTestServerWithXfccConfig("always_forward_only", "");
  testRequestAndResponseWithXfccHeader(makeClientConnection(), previous_xfcc_, previous_xfcc_);
}

TEST_P(XfccIntegrationTest, NonTlsEnforceSanitize) {
  // The forward_only, append_forward and sanitize_set options are not effective when the connection
  // is not using Mtls.
  startTestServerWithXfccConfig("forward_only", "");
  testRequestAndResponseWithXfccHeader(makeClientConnection(), previous_xfcc_, "");
}
} // Xfcc
} // Envoy
