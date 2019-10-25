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

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

// Validate certificate selection across different certificate types and client TLS versions.
class SslCertficateIntegrationTest
    : public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, envoy::api::v2::auth::TlsParameters_TlsProtocol>>,
      public SslIntegrationTestBase {
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
      // The test supports tapping either the downstream or upstream connection, but not both.
      if (upstream_tap_) {
        setupUpstreamTap(bootstrap);
      } else {
        setupDownstreamTap(bootstrap);
      }
    });
    SslIntegrationTest::initialize();
    // This confuses our socket counting.
    debug_with_s_client_ = false;
  }

  void setupUpstreamTap(envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* transport_socket =
        bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
    transport_socket->set_name("envoy.transport_sockets.tap");
    envoy::api::v2::core::TransportSocket raw_transport_socket;
    raw_transport_socket.set_name("raw_buffer");
    envoy::config::transport_socket::tap::v2alpha::Tap tap_config =
        createTapConfig(raw_transport_socket);
    tap_config.mutable_transport_socket()->MergeFrom(raw_transport_socket);
    transport_socket->mutable_typed_config()->PackFrom(tap_config);
  }

  void setupDownstreamTap(envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    // Configure inner SSL transport socket based on existing config.
    envoy::api::v2::core::TransportSocket ssl_transport_socket;
    auto* transport_socket = filter_chain->mutable_transport_socket();
    ssl_transport_socket.Swap(transport_socket);
    // Configure outer tap transport socket.
    transport_socket->set_name("envoy.transport_sockets.tap");
    envoy::config::transport_socket::tap::v2alpha::Tap tap_config =
        createTapConfig(ssl_transport_socket);
    tap_config.mutable_transport_socket()->MergeFrom(ssl_transport_socket);
    transport_socket->mutable_typed_config()->PackFrom(tap_config);
  }

  envoy::config::transport_socket::tap::v2alpha::Tap
  createTapConfig(const envoy::api::v2::core::TransportSocket& inner_transport) {
    envoy::config::transport_socket::tap::v2alpha::Tap tap_config;
    tap_config.mutable_common_config()
        ->mutable_static_config()
        ->mutable_match_config()
        ->set_any_match(true);
    auto* output_config =
        tap_config.mutable_common_config()->mutable_static_config()->mutable_output_config();
    if (max_rx_bytes_.has_value()) {
      output_config->mutable_max_buffered_rx_bytes()->set_value(max_rx_bytes_.value());
    }
    if (max_tx_bytes_.has_value()) {
      output_config->mutable_max_buffered_tx_bytes()->set_value(max_tx_bytes_.value());
    }

    auto* output_sink = output_config->mutable_sinks()->Add();
    output_sink->set_format(format_);
    output_sink->mutable_file_per_tap()->set_path_prefix(path_prefix_);
    tap_config.mutable_transport_socket()->MergeFrom(inner_transport);
    return tap_config;
  }

  std::string path_prefix_ = TestEnvironment::temporaryPath("ssl_trace");
  envoy::service::tap::v2alpha::OutputSink::Format format_{
      envoy::service::tap::v2alpha::OutputSink::PROTO_BINARY};
  absl::optional<uint64_t> max_rx_bytes_;
  absl::optional<uint64_t> max_tx_bytes_;
  bool upstream_tap_{};
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
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
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
  envoy::data::tap::v2alpha::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, first_id), trace, *api_);
  // Validate general expected properties in the trace.
  EXPECT_EQ(first_id, trace.socket_buffered_trace().trace_id());
  EXPECT_THAT(expected_local_address,
              ProtoEq(trace.socket_buffered_trace().connection().local_address()));
  EXPECT_THAT(expected_remote_address,
              ProtoEq(trace.socket_buffered_trace().connection().remote_address()));
  ASSERT_GE(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data().as_bytes(),
                               "POST /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(1).write().data().as_bytes(),
                               "HTTP/1.1 200 OK"));
  EXPECT_FALSE(trace.socket_buffered_trace().read_truncated());
  EXPECT_FALSE(trace.socket_buffered_trace().write_truncated());

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
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(256, response->body().size());
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 2);
  TestUtility::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, second_id), trace, *api_);
  // Validate second connection ID.
  EXPECT_EQ(second_id, trace.socket_buffered_trace().trace_id());
  ASSERT_GE(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data().as_bytes(),
                               "GET /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(1).write().data().as_bytes(),
                               "HTTP/1.1 200 OK"));
  EXPECT_FALSE(trace.socket_buffered_trace().read_truncated());
  EXPECT_FALSE(trace.socket_buffered_trace().write_truncated());
}

// Verify that truncation works correctly across multiple transport socket frames.
TEST_P(SslTapIntegrationTest, TruncationWithMultipleDataFrames) {
  max_rx_bytes_ = 4;
  max_tx_bytes_ = 5;

  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  codec_client_ = makeHttpConnection(creator());
  const Http::TestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  auto decoder = std::move(result.second);
  Buffer::OwnedImpl data1("one");
  result.first.encodeData(data1, false);
  Buffer::OwnedImpl data2("two");
  result.first.encodeData(data2, true);
  waitForNextUpstreamRequest();
  const Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl data3("three");
  upstream_request_->encodeData(data3, false);
  decoder->waitForBodyData(5);
  Buffer::OwnedImpl data4("four");
  upstream_request_->encodeData(data4, true);
  decoder->waitForEndStream();

  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  envoy::data::tap::v2alpha::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, id), trace, *api_);

  ASSERT_EQ(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(trace.socket_buffered_trace().events(0).read().data().truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().events(1).write().data().truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().write_truncated());
}

// Validate a single request with text proto output.
TEST_P(SslTapIntegrationTest, RequestWithTextProto) {
  format_ = envoy::service::tap::v2alpha::OutputSink::PROTO_TEXT;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  testRouterRequestAndResponseWithBody(1024, 512, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::data::tap::v2alpha::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb_text", path_prefix_, id), trace, *api_);
  // Test some obvious properties.
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data().as_bytes(),
                               "POST /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(1).write().data().as_bytes(),
                               "HTTP/1.1 200 OK"));
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_FALSE(trace.socket_buffered_trace().write_truncated());
}

// Validate a single request with JSON (body as string) output. This test uses an upstream tap.
TEST_P(SslTapIntegrationTest, RequestWithJsonBodyAsStringUpstreamTap) {
  upstream_tap_ = true;
  max_rx_bytes_ = 5;
  max_tx_bytes_ = 4;

  format_ = envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 2;
  testRouterRequestAndResponseWithBody(512, 1024, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  test_server_.reset();

  // This must be done after server shutdown so that connection pool connections are closed and
  // the tap written.
  envoy::data::tap::v2alpha::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.json", path_prefix_, id), trace, *api_);

  // Test some obvious properties.
  EXPECT_EQ(trace.socket_buffered_trace().events(0).write().data().as_string(), "POST");
  EXPECT_EQ(trace.socket_buffered_trace().events(1).read().data().as_string(), "HTTP/");
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().write_truncated());
}

} // namespace Ssl
} // namespace Envoy
