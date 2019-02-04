#include "ssl_integration_test.h"

#include <memory>
#include <string>

#include "envoy/config/transport_socket/tap/v2alpha/tap.pb.h"
#include "envoy/data/tap/v2alpha/wrapper.pb.h"

#include "common/event/dispatcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "integration.h"
#include "utility.h"

using testing::Return;

namespace Envoy {
namespace Ssl {

void SslIntegrationTestBase::initialize() {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(server_rsa_cert_)
                                  .setEcdsaCert(server_ecdsa_cert_)
                                  .setTlsV13(server_tlsv1_3_)
                                  .setExpectClientEcdsaCert(client_ecdsa_cert_));
  HttpIntegrationTest::initialize();

  context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());

  registerTestServerPorts({"http"});
}

void SslIntegrationTestBase::TearDown() {
  HttpIntegrationTest::cleanupUpstreamAndDownstream();
  codec_client_.reset();
  context_manager_.reset();
}

Network::ClientConnectionPtr
SslIntegrationTestBase::makeSslClientConnection(const ClientSslTransportOptions& options) {
  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  if (debug_with_s_client_) {
    const std::string s_client_cmd = TestEnvironment::substitute(
        "openssl s_client -connect " + address->asString() +
            " -showcerts -debug -msg -CAfile "
            "{{ test_rundir }}/test/config/integration/certs/cacert.pem "
            "-servername lyft.com -cert "
            "{{ test_rundir }}/test/config/integration/certs/clientcert.pem "
            "-key "
            "{{ test_rundir }}/test/config/integration/certs/clientkey.pem ",
        version_);
    ENVOY_LOG_MISC(debug, "Executing {}", s_client_cmd);
    RELEASE_ASSERT(::system(s_client_cmd.c_str()) == 0, "");
  }
  auto client_transport_socket_factory_ptr =
      createClientSslTransportSocketFactory(options, *context_manager_, *api_);
  return dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}), nullptr);
}

void SslIntegrationTestBase::checkStats() {
  const uint32_t expected_handshakes = debug_with_s_client_ ? 2 : 1;
  Stats::CounterSharedPtr counter = test_server_->counter(listenerStatPrefix("ssl.handshake"));
  EXPECT_EQ(expected_handshakes, counter->value());
  counter->reset();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, SslIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2) {
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  config_helper_.setClientCodec(
      envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::AUTO);
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setAlpn(true));
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferVerifySAN) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setSan(true));
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2VerifySAN) {
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setAlpn(true).setSan(true));
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterHeaderOnlyRequestAndResponse) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterHeaderOnlyRequestAndResponse(&creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterUpstreamDisconnectBeforeResponseComplete(&creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterDownstreamDisconnectBeforeRequestComplete(&creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
#ifdef __APPLE__
  // Skip this test on macOS: we can't detect the early close on macOS, and we
  // won't clean up the upstream connection until it times out. See #4294.
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    return;
  }
#endif
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterDownstreamDisconnectBeforeResponseComplete(&creator);
  checkStats();
}

// This test must be here vs integration_admin_test so that it tests a server with loaded certs.
TEST_P(SslIntegrationTest, AdminCertEndpoint) {
  initialize();
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/certs", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

// Validate certificate selection across different certificate types and client TLS versions.
class SslCertficateIntegrationTest
    : public SslIntegrationTestBase,
      public TestBaseWithParam<std::tuple<Network::Address::IpVersion,
                                          envoy::api::v2::auth::TlsParameters_TlsProtocol>> {
public:
  SslCertficateIntegrationTest() : SslIntegrationTestBase(std::get<0>(GetParam())) {
    server_tlsv1_3_ = true;
  }

  Network::ClientConnectionPtr
  makeSslClientConnection(const ClientSslTransportOptions& options) override {
    ClientSslTransportOptions modified_options{options};
    modified_options.setTlsVersion(tls_version_);
    return SslIntegrationTestBase::makeSslClientConnection(modified_options);
  }

  void TearDown() override { SslIntegrationTestBase::TearDown(); };

  ClientSslTransportOptions rsaOnlyClientOptions() {
    if (tls_version_ == envoy::api::v2::auth::TlsParameters::TLSv1_3) {
      return ClientSslTransportOptions().setSigningAlgorithmsForTest("rsa_pss_rsae_sha256");
    } else {
      return ClientSslTransportOptions().setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
    }
  }

  ClientSslTransportOptions ecdsaOnlyClientOptions() {
    auto options = ClientSslTransportOptions().setClientEcdsaCert(true);
    if (tls_version_ == envoy::api::v2::auth::TlsParameters::TLSv1_3) {
      return options.setSigningAlgorithmsForTest("ecdsa_secp256r1_sha256");
    } else {
      return options.setCipherSuites({"ECDHE-ECDSA-AES128-GCM-SHA256"});
    }
  }

  static std::string ipClientVersionTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<Network::Address::IpVersion, envoy::api::v2::auth::TlsParameters_TlsProtocol>>&
          params) {
    return fmt::format("{}_TLSv1_{}",
                       std::get<0>(params.param) == Network::Address::IpVersion::v4 ? "IPv4"
                                                                                    : "IPv6",
                       std::get<1>(params.param) - 1);
  }

  const envoy::api::v2::auth::TlsParameters_TlsProtocol tls_version_{std::get<1>(GetParam())};
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientVersions, SslCertficateIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(envoy::api::v2::auth::TlsParameters::TLSv1_2,
                                     envoy::api::v2::auth::TlsParameters::TLSv1_3)),
    SslCertficateIntegrationTest::ipClientVersionTestParamsToString);

// Server with an RSA certificate and a client with RSA/ECDSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerRsa) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = false;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

// Server with an ECDSA certificate and a client with RSA/ECDSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerEcdsa) {
  server_rsa_cert_ = false;
  server_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

// Server with RSA/ECDSAs certificates and a client with RSA/ECDSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerRsaEcdsa) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

// Server with an RSA certificate and a client with only RSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ClientRsaOnly) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = false;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(rsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

// Server has only an ECDSA certificate, client is only RSA capable, leads to a connection fail.
TEST_P(SslCertficateIntegrationTest, ServerEcdsaClientRsaOnly) {
  server_rsa_cert_ = false;
  server_ecdsa_cert_ = true;
  initialize();
  auto codec_client = makeRawHttpConnection(makeSslClientConnection(rsaOnlyClientOptions()));
  EXPECT_FALSE(codec_client->connected());
  const std::string counter_name = listenerStatPrefix("ssl.connection_error");
  Stats::CounterSharedPtr counter = test_server_->counter(counter_name);
  test_server_->waitForCounterGe(counter_name, 1);
  EXPECT_EQ(1U, counter->value());
  counter->reset();
}

// Server with RSA/ECDSA certificates and a client with only RSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerRsaEcdsaClientRsaOnly) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(rsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

// Server has only an RSA certificate, client is only ECDSA capable, leads to connection fail.
TEST_P(SslCertficateIntegrationTest, ServerRsaClientEcdsaOnly) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = false;
  client_ecdsa_cert_ = true;
  initialize();
  EXPECT_FALSE(
      makeRawHttpConnection(makeSslClientConnection(ecdsaOnlyClientOptions()))->connected());
  const std::string counter_name = listenerStatPrefix("ssl.connection_error");
  Stats::CounterSharedPtr counter = test_server_->counter(counter_name);
  test_server_->waitForCounterGe(counter_name, 1);
  EXPECT_EQ(1U, counter->value());
  counter->reset();
}

// Server has only an ECDSA certificate, client is only ECDSA capable works.
TEST_P(SslCertficateIntegrationTest, ServerEcdsaClientEcdsaOnly) {
  server_rsa_cert_ = false;
  server_ecdsa_cert_ = true;
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

// Server has RSA/ECDSA certificates, client is only ECDSA capable works.
TEST_P(SslCertficateIntegrationTest, ServerRsaEcdsaClientEcdsaOnly) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
}

// TODO(mattklein123): Move this into a dedicated integration test for the tap transport socket as
// well as add more tests.
class SslTapIntegrationTest : public SslIntegrationTest {
public:
  void initialize() override {
    // TODO(mattklein123): Merge/use the code in ConfigHelper::setTapTransportSocket().
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      // Configure inner SSL transport socket based on existing config.
      envoy::api::v2::core::TransportSocket ssl_transport_socket;
      ssl_transport_socket.set_name("tls");
      MessageUtil::jsonConvert(filter_chain->tls_context(), *ssl_transport_socket.mutable_config());
      // Configure outer tap transport socket.
      auto* transport_socket = filter_chain->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.tap");
      envoy::config::transport_socket::tap::v2alpha::Tap tap_config;
      tap_config.mutable_common_config()
          ->mutable_static_config()
          ->mutable_match_config()
          ->set_any_match(true);
      auto* file_sink = tap_config.mutable_common_config()
                            ->mutable_static_config()
                            ->mutable_output_config()
                            ->mutable_sinks()
                            ->Add()
                            ->mutable_file_per_tap();
      file_sink->set_path_prefix(path_prefix_);
      file_sink->set_format(text_format_
                                ? envoy::service::tap::v2alpha::FilePerTapSink::PROTO_TEXT
                                : envoy::service::tap::v2alpha::FilePerTapSink::PROTO_BINARY);
      tap_config.mutable_transport_socket()->MergeFrom(ssl_transport_socket);
      MessageUtil::jsonConvert(tap_config, *transport_socket->mutable_config());
      // Nuke TLS context from legacy location.
      filter_chain->clear_tls_context();
      // Rest of TLS initialization.
    });
    SslIntegrationTest::initialize();
    // This confuses our socket counting.
    debug_with_s_client_ = false;
  }

  std::string path_prefix_ = TestEnvironment::temporaryPath("ssl_trace");
  bool text_format_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslTapIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate two back-to-back requests with binary proto output.
TEST_P(SslTapIntegrationTest, TwoRequestsWithBinaryProto) {
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // First request (ID will be +1 since the client will also bump).
  const uint64_t first_id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  codec_client_ = makeHttpConnection(creator());
  Http::TestHeaderMapImpl post_request_headers{
      {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  auto response =
      sendRequestAndWaitForResponse(post_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(256, response->body().size());
  checkStats();
  envoy::api::v2::core::Address expected_local_address;
  Network::Utility::addressToProtobufAddress(*codec_client_->connection()->remoteAddress(),
                                             expected_local_address);
  envoy::api::v2::core::Address expected_remote_address;
  Network::Utility::addressToProtobufAddress(*codec_client_->connection()->localAddress(),
                                             expected_remote_address);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::data::tap::v2alpha::BufferedTraceWrapper trace;
  MessageUtil::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, first_id), trace, *api_);
  // Validate general expected properties in the trace.
  EXPECT_EQ(first_id, trace.socket_buffered_trace().connection().id());
  EXPECT_THAT(expected_local_address,
              ProtoEq(trace.socket_buffered_trace().connection().local_address()));
  EXPECT_THAT(expected_remote_address,
              ProtoEq(trace.socket_buffered_trace().connection().remote_address()));
  ASSERT_GE(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data(),
                               "POST /test/long/url HTTP/1.1"));
  EXPECT_TRUE(
      absl::StartsWith(trace.socket_buffered_trace().events(1).write().data(), "HTTP/1.1 200 OK"));

  // Verify a second request hits a different file.
  const uint64_t second_id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  codec_client_ = makeHttpConnection(creator());
  Http::TestHeaderMapImpl get_request_headers{
      {":method", "GET"},     {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  response =
      sendRequestAndWaitForResponse(get_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(256, response->body().size());
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 2);
  MessageUtil::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, second_id), trace, *api_);
  // Validate second connection ID.
  EXPECT_EQ(second_id, trace.socket_buffered_trace().connection().id());
  ASSERT_GE(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data(),
                               "GET /test/long/url HTTP/1.1"));
  EXPECT_TRUE(
      absl::StartsWith(trace.socket_buffered_trace().events(1).write().data(), "HTTP/1.1 200 OK"));
}

// Validate a single request with text proto output.
TEST_P(SslTapIntegrationTest, RequestWithTextProto) {
  text_format_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::data::tap::v2alpha::BufferedTraceWrapper trace;
  MessageUtil::loadFromFile(fmt::format("{}_{}.pb_text", path_prefix_, id), trace, *api_);
  // Test some obvious properties.
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data(),
                               "POST /test/long/url HTTP/1.1"));
  EXPECT_TRUE(
      absl::StartsWith(trace.socket_buffered_trace().events(1).write().data(), "HTTP/1.1 200 OK"));
}

} // namespace Ssl
} // namespace Envoy
