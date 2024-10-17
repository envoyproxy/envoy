#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/utility.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/ssl_handshaker.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/common/tls/cert_validator/timed_cert_validator.h"
#include "test/common/tls/integration/ssl_integration_test_base.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::StartsWith;

namespace Envoy {

using Extensions::TransportSockets::Tls::ContextImplPeer;

namespace Ssl {

class SslIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public SslIntegrationTestBase {
public:
  SslIntegrationTest() : SslIntegrationTestBase(GetParam()) {}
  void TearDown() override { SslIntegrationTestBase::TearDown(); };
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that Envoy behaves correctly when receiving an SSLAlert for an unspecified code. The codes
// are defined in the standard, and assigned codes have a string associated with them in BoringSSL,
// which is included in logs. For an unknown code, verify that no crash occurs.
TEST_P(SslIntegrationTest, UnknownSslAlert) {
  initialize();
  Network::ClientConnectionPtr connection = makeSslClientConnection({});
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  while (!callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Ssl::ConnectionInfoConstSharedPtr ssl_info = connection->ssl();
  SSL* ssl =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(ssl_info.get())
          ->ssl();
  ASSERT_EQ(connection->state(), Network::Connection::State::Open);
  ASSERT_NE(ssl, nullptr);
  SSL_send_fatal_alert(ssl, 255);
  while (!callbacks.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  const std::string counter_name = listenerStatPrefix("ssl.connection_error");
  Stats::CounterSharedPtr counter = test_server_->counter(counter_name);
  test_server_->waitForCounterGe(counter_name, 1);
  connection->close(Network::ConnectionCloseType::NoFlush);
}

// Test that stats produced by the tls transport socket have correct tag extraction.
TEST_P(SslIntegrationTest, StatsTagExtraction) {
  // Configure TLS to use specific parameters so the exact metrics the test expects are created.
  // TLSv1.3 doesn't allow specifying the cipher suites, so use TLSv1.2 to force a specific cipher
  // suite to simplify the test.
  // Use P-256 to test the regex on a curve containing a hyphen (instead of X25519).

  // Configure test-client to Envoy connection.
  server_curves_.push_back("P-256");
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(
        ClientSslTransportOptions{}
            .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2)
            .setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"})
            .setSigningAlgorithms({"rsa_pss_rsae_sha256"}));
  };

  // Configure Envoy to fake-upstream connection.
  upstream_tls_ = true;
  setUpstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.configureUpstreamTls(
      false, false, absl::nullopt,
      [](envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& ctx) {
        auto& params = *ctx.mutable_tls_params();
        params.set_tls_minimum_protocol_version(
            envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
        params.set_tls_maximum_protocol_version(
            envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
        params.add_ecdh_curves("P-256");
        params.add_signature_algorithms("rsa_pss_rsae_sha256");
        params.add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
      });

  testRouterRequestAndResponseWithBody(1024, 1024, false, false, &creator);
  checkStats();

  using ExpectedResultsMap =
      absl::node_hash_map<std::string, std::pair<std::string, Stats::TagVector>>;
  ExpectedResultsMap base_expected_counters = {
      {"ssl.ciphers.ECDHE-RSA-AES128-GCM-SHA256",
       {"ssl.ciphers", {{"envoy.ssl_cipher", "ECDHE-RSA-AES128-GCM-SHA256"}}}},
      {"ssl.versions.TLSv1.2", {"ssl.versions", {{"envoy.ssl_version", "TLSv1.2"}}}},
      {"ssl.curves.P-256", {"ssl.curves", {{"envoy.ssl_curve", "P-256"}}}},
      {"ssl.sigalgs.rsa_pss_rsae_sha256",
       {"ssl.sigalgs", {{"envoy.ssl_sigalg", "rsa_pss_rsae_sha256"}}}},
  };

  // Expect all the stats for both listeners and clusters.
  ExpectedResultsMap expected_counters;
  for (const auto& entry : base_expected_counters) {
    expected_counters[listenerStatPrefix(entry.first)] = {
        absl::StrCat("listener.", entry.second.first), entry.second.second};
    expected_counters[absl::StrCat("cluster.cluster_0.", entry.first)] = {
        absl::StrCat("cluster.", entry.second.first), entry.second.second};
  }

  // The cipher suite extractor is written as two rules for listener and cluster, and they don't
  // match unfortunately, but it's left this way for backwards compatibility.
  expected_counters["cluster.cluster_0.ssl.ciphers.ECDHE-RSA-AES128-GCM-SHA256"].second = {
      {"cipher_suite", "ECDHE-RSA-AES128-GCM-SHA256"}};

  for (const Stats::CounterSharedPtr& counter : test_server_->counters()) {
    // Useful for debugging when the test is failing.
    if (counter->name().find("ssl") != std::string::npos) {
      ENVOY_LOG_MISC(critical, "Found ssl metric: {}", counter->name());
    }
    auto it = expected_counters.find(counter->name());
    if (it != expected_counters.end()) {
      EXPECT_EQ(counter->tagExtractedName(), it->second.first);

      // There are other extracted tags such as listener and cluster name, hence ``IsSupersetOf``.
      EXPECT_THAT(counter->tags(), ::testing::IsSupersetOf(it->second.second));
      expected_counters.erase(it);
    }
  }

  EXPECT_THAT(expected_counters, ::testing::IsEmpty());
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, Http1StreamInfoDownstreamHandshakeTiming) {
  ASSERT_TRUE(downstreamProtocol() == Http::CodecType::HTTP1);
  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  initialize();
  codec_client_ = makeHttpConnection(makeSslClientConnection({}));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_FALSE(
      response->headers().get(Http::LowerCaseString("downstream_handshake_complete")).empty());
}

TEST_P(SslIntegrationTest, Http2StreamInfoDownstreamHandshakeTiming) {
  // See MultiplexedIntegrationTest for equivalent test for HTTP/3.
  setDownstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  initialize();
  codec_client_ = makeHttpConnection(makeSslClientConnection({}));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_FALSE(
      response->headers().get(Http::LowerCaseString("downstream_handshake_complete")).empty());
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  config_helper_.setClientCodec(envoy::extensions::filters::network::http_connection_manager::v3::
                                    HttpConnectionManager::AUTO);
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setAlpn(true));
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferVerifySAN) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setSan(san_to_match_));
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

TEST_P(SslIntegrationTest, RouterRequestAndResponseWithBodyNoBufferHttp2VerifySAN) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setAlpn(true).setSan(san_to_match_));
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
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
#if defined(__APPLE__) || defined(WIN32)
  // Skip this test on OS X + Windows: we can't detect the early close on non-Linux, and we
  // won't clean up the upstream connection until it times out. See #4294.
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    return;
  }
#endif
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterDownstreamDisconnectBeforeResponseComplete(&creator);
  checkStats();
}

// Test server preference of cipher suites. Server order is ECDHE-RSA-AES128-GCM-SHA256
// followed by ECDHE-RSA-AES256-GCM-SHA384. "ECDHE-RSA-AES128-GCM-SHA256" should be used based on
// server preference.
TEST_P(SslIntegrationTest, TestServerCipherPreference) {
  server_ciphers_.push_back("ECDHE-RSA-AES128-GCM-SHA256");
  server_ciphers_.push_back("ECDHE-RSA-AES256-GCM-SHA384");
  initialize();
  codec_client_ = makeHttpConnection(makeSslClientConnection(
      ClientSslTransportOptions{}
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2)
          .setCipherSuites({"ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256"})));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  const std::string counter_name = listenerStatPrefix("ssl.ciphers.ECDHE-RSA-AES128-GCM-SHA256");
  Stats::CounterSharedPtr counter = test_server_->counter(counter_name);
  EXPECT_EQ(1, test_server_->counter(counter_name)->value());
}

// Test client preference of cipher suites. Same server preference is followed as in the previous.
// "ECDHE-RSA-AES256-GCM-SHA384" should be used based on client preference.
TEST_P(SslIntegrationTest, ClientCipherPreference) {
  prefer_client_ciphers_ = true;
  server_ciphers_.push_back("ECDHE-RSA-AES128-GCM-SHA256");
  server_ciphers_.push_back("ECDHE-RSA-AES256-GCM-SHA384");
  initialize();
  codec_client_ = makeHttpConnection(makeSslClientConnection(
      ClientSslTransportOptions{}
          .setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2)
          .setCipherSuites({"ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256"})));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  const std::string counter_name = listenerStatPrefix("ssl.ciphers.ECDHE-RSA-AES256-GCM-SHA384");
  Stats::CounterSharedPtr counter = test_server_->counter(counter_name);
  EXPECT_EQ(1, test_server_->counter(counter_name)->value());
}

// This test must be here vs integration_admin_test so that it tests a server with loaded certs.
TEST_P(SslIntegrationTest, AdminCertEndpoint) {
  DISABLE_IF_ADMIN_DISABLED; // Admin functionality.
  initialize();
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/certs", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(SslIntegrationTest, RouterHeaderOnlyRequestAndResponseWithSni) {
  config_helper_.addFilter("name: sni-to-header-filter");
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setSni("host.com"));
  };
  initialize();
  codec_client_ = makeHttpConnection(
      makeSslClientConnection(ClientSslTransportOptions().setSni("www.host.com")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "https"}, {":authority", "host.com"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  EXPECT_EQ("www.host.com", upstream_request_->headers()
                                .get(Http::LowerCaseString("x-envoy-client-sni"))[0]
                                ->value()
                                .getStringView());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);
  RELEASE_ASSERT(response->waitForEndStream(), "unexpected timeout");

  checkStats();
}

TEST_P(SslIntegrationTest, LogPeerIpSanUnsupportedIpVersion) {
  useListenerAccessLog("%DOWNSTREAM_PEER_IP_SAN%");
  config_helper_.addFilter("name: sni-to-header-filter");
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ClientSslTransportOptions().setSni("host.com"));
  };
  initialize();
  codec_client_ = makeHttpConnection(
      makeSslClientConnection(ClientSslTransportOptions().setSni("www.host.com")));

  // Disable IP version for the alternate type from the test. The client cert has both an ipv4 and
  // an ipv6 SAN. This must happen after the client has loaded the cert to send as the client cert.
  auto disabler = (version_ == Network::Address::IpVersion::v4)
                      ? Network::Address::Ipv6Instance::forceProtocolUnsupportedForTest
                      : Network::Address::Ipv4Instance::forceProtocolUnsupportedForTest;
  Cleanup cleaner(disabler(true));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "https"}, {":authority", "host.com"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  EXPECT_EQ("www.host.com", upstream_request_->headers()
                                .get(Http::LowerCaseString("x-envoy-client-sni"))[0]
                                ->value()
                                .getStringView());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);
  RELEASE_ASSERT(response->waitForEndStream(), "unexpected timeout");
  codec_client_->close();

  checkStats();
  auto result = waitForAccessLog(listener_access_log_name_);
  if (version_ == Network::Address::IpVersion::v4) {
    EXPECT_EQ(result, "1.2.3.4");
  } else {
    EXPECT_EQ(result, "0:1:2:3::4");
  }
}

TEST_P(SslIntegrationTest, AsyncCertValidationSucceeds) {
  // Config client to use an async cert validator which defer the actual validation by 5ms.
  auto custom_validator_config = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
      envoy::config::core::v3::TypedExtensionConfig());
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  initialize();

  Network::ClientConnectionPtr connection = makeSslClientConnection(
      ClientSslTransportOptions().setCustomCertValidatorConfig(custom_validator_config.get()));
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      connection->ssl().get());
  ASSERT(socket);
  while (socket->state() == Ssl::SocketState::PreHandshake) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_EQ(connection->state(), Network::Connection::State::Open);
  while (!callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  connection->close(Network::ConnectionCloseType::NoFlush);
}

TEST_P(SslIntegrationTest, AsyncCertValidationSucceedsWithLocalAddress) {
  auto custom_validator_config = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
      envoy::config::core::v3::TypedExtensionConfig());
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  auto* cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  initialize();
  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  auto client_transport_socket_factory_ptr = createClientSslTransportSocketFactory(
      ClientSslTransportOptions().setCustomCertValidatorConfig(custom_validator_config.get()),
      *context_manager_, *api_);
  Network::ClientConnectionPtr connection = dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);

  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();

  // Get the `TimedCertValidator` object and set its expected local address.
  Envoy::Ssl::ClientContextSharedPtr client_ssl_ctx =
      static_cast<Extensions::TransportSockets::Tls::ClientSslSocketFactory&>(
          *client_transport_socket_factory_ptr)
          .sslCtx();
  Extensions::TransportSockets::Tls::TimedCertValidator& cert_validator =
      static_cast<Extensions::TransportSockets::Tls::TimedCertValidator&>(
          ContextImplPeer::getMutableCertValidator(
              static_cast<Extensions::TransportSockets::Tls::ClientContextImpl&>(*client_ssl_ctx)));
  ASSERT_TRUE(connection->connectionInfoProvider().localAddress() != nullptr);
  cert_validator.setExpectedLocalAddress(
      connection->connectionInfoProvider().localAddress()->asString());

  const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      connection->ssl().get());
  ASSERT(socket);
  while (socket->state() == Ssl::SocketState::PreHandshake) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  ASSERT_EQ(connection->state(), Network::Connection::State::Open);
  while (!callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  connection->close(Network::ConnectionCloseType::NoFlush);
}

TEST_P(SslIntegrationTest, AsyncCertValidationAfterTearDown) {
  auto custom_validator_config = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
      envoy::config::core::v3::TypedExtensionConfig());
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  auto* cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(1000));
  initialize();
  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  auto client_transport_socket_factory_ptr = createClientSslTransportSocketFactory(
      ClientSslTransportOptions().setCustomCertValidatorConfig(custom_validator_config.get()),
      *context_manager_, *api_);
  Network::ClientConnectionPtr connection = dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      connection->ssl().get());
  ASSERT(socket);
  while (socket->state() == Ssl::SocketState::PreHandshake) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  Envoy::Ssl::ClientContextSharedPtr client_ssl_ctx =
      static_cast<Extensions::TransportSockets::Tls::ClientSslSocketFactory&>(
          *client_transport_socket_factory_ptr)
          .sslCtx();
  auto& cert_validator = static_cast<const Extensions::TransportSockets::Tls::TimedCertValidator&>(
      ContextImplPeer::getCertValidator(
          static_cast<Extensions::TransportSockets::Tls::ClientContextImpl&>(*client_ssl_ctx)));
  EXPECT_TRUE(cert_validator.validationPending());
  ASSERT_EQ(connection->state(), Network::Connection::State::Open);
  connection->close(Network::ConnectionCloseType::NoFlush);
  connection.reset();
  while (cert_validator.validationPending()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_P(SslIntegrationTest, AsyncCertValidationAfterSslShutdown) {
  auto custom_validator_config = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
      envoy::config::core::v3::TypedExtensionConfig());
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  auto* cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(1000));
  initialize();
  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  auto client_transport_socket_factory_ptr = createClientSslTransportSocketFactory(
      ClientSslTransportOptions().setCustomCertValidatorConfig(custom_validator_config.get()),
      *context_manager_, *api_);
  Network::ClientConnectionPtr connection = dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      connection->ssl().get());
  ASSERT(socket);
  while (socket->state() == Ssl::SocketState::PreHandshake) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  Envoy::Ssl::ClientContextSharedPtr client_ssl_ctx =
      static_cast<Extensions::TransportSockets::Tls::ClientSslSocketFactory&>(
          *client_transport_socket_factory_ptr)
          .sslCtx();
  auto& cert_validator = static_cast<const Extensions::TransportSockets::Tls::TimedCertValidator&>(
      ContextImplPeer::getCertValidator(
          static_cast<Extensions::TransportSockets::Tls::ClientContextImpl&>(*client_ssl_ctx)));
  EXPECT_TRUE(cert_validator.validationPending());
  ASSERT_EQ(connection->state(), Network::Connection::State::Open);
  connection->close(Network::ConnectionCloseType::NoFlush);
  while (cert_validator.validationPending()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  connection.reset();
}

class RawWriteSslIntegrationTest : public SslIntegrationTest {
protected:
  std::unique_ptr<Http::TestRequestHeaderMapImpl>
  testFragmentedRequestWithBufferLimit(std::list<std::string> request_chunks,
                                       uint32_t buffer_limit) {
    autonomous_upstream_ = true;
    config_helper_.setBufferLimits(buffer_limit, buffer_limit);
    initialize();

    // write_request_cb will write each of the items in request_chunks as a separate SSL_write.
    auto write_request_cb = [&request_chunks](Buffer::Instance& buffer) {
      if (!request_chunks.empty()) {
        buffer.add(request_chunks.front());
        request_chunks.pop_front();
      }
      return false;
    };

    auto client_transport_socket_factory_ptr =
        createClientSslTransportSocketFactory({}, *context_manager_, *api_);
    std::string response;
    auto connection = createConnectionDriver(
        lookupPort("http"), write_request_cb,
        [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
          response.append(data.toString());
        },
        client_transport_socket_factory_ptr->createTransportSocket({}, nullptr));

    // Drive the connection until we get a response.
    while (response.empty()) {
      EXPECT_TRUE(connection->run(Event::Dispatcher::RunType::NonBlock));
    }
    EXPECT_THAT(response, testing::HasSubstr("HTTP/1.1 200 OK\r\n"));

    connection->close();
    return reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
        ->lastRequestHeaders();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RawWriteSslIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Regression test for https://github.com/envoyproxy/envoy/issues/12304
TEST_P(RawWriteSslIntegrationTest, HighWatermarkReadResumptionProcessingHeaders) {
  // The raw writer will perform a separate SSL_write for each of the chunks below. Chunk sizes were
  // picked such that the connection's high watermark will trigger while processing the last SSL
  // record containing the request headers. Verify that read resumption works correctly after
  // hitting the receive buffer high watermark.
  std::list<std::string> request_chunks = {
      "GET / HTTP/1.1\r\nHost: host\r\n",
      "key1:" + std::string(14000, 'a') + "\r\n",
      "key2:" + std::string(16000, 'b') + "\r\n\r\n",
  };

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      testFragmentedRequestWithBufferLimit(request_chunks, 15 * 1024);
  ASSERT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "host");
  EXPECT_EQ(
      std::string(14000, 'a'),
      upstream_headers->get(Envoy::Http::LowerCaseString("key1"))[0]->value().getStringView());
  EXPECT_EQ(
      std::string(16000, 'b'),
      upstream_headers->get(Envoy::Http::LowerCaseString("key2"))[0]->value().getStringView());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/12304
TEST_P(RawWriteSslIntegrationTest, HighWatermarkReadResumptionProcesingBody) {
  // The raw writer will perform a separate SSL_write for each of the chunks below. Chunk sizes were
  // picked such that the connection's high watermark will trigger while processing the last SSL
  // record containing the POST body. Verify that read resumption works correctly after hitting the
  // receive buffer high watermark.
  std::list<std::string> request_chunks = {
      "POST / HTTP/1.1\r\nHost: host\r\ncontent-length: 30000\r\n\r\n",
      std::string(14000, 'a'),
      std::string(16000, 'a'),
  };

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      testFragmentedRequestWithBufferLimit(request_chunks, 15 * 1024);
  ASSERT_TRUE(upstream_headers != nullptr);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/12304
TEST_P(RawWriteSslIntegrationTest, HighWatermarkReadResumptionProcesingLargerBody) {
  std::list<std::string> request_chunks = {
      "POST / HTTP/1.1\r\nHost: host\r\ncontent-length: 150000\r\n\r\n",
  };
  for (int i = 0; i < 10; ++i) {
    request_chunks.push_back(std::string(15000, 'a'));
  }

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers =
      testFragmentedRequestWithBufferLimit(request_chunks, 16 * 1024);
  ASSERT_TRUE(upstream_headers != nullptr);
}

// Validate certificate selection across different certificate types and client TLS versions.
class SslCertficateIntegrationTest
    : public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion,
                     envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol>>,
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
    if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
      return ClientSslTransportOptions().setSigningAlgorithms({"rsa_pss_rsae_sha256"});
    } else {
      return ClientSslTransportOptions().setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
    }
  }

  ClientSslTransportOptions ecdsaOnlyClientOptions() {
    auto options = ClientSslTransportOptions().setClientEcdsaCert(true);
    if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
      return options.setSigningAlgorithms({"ecdsa_secp256r1_sha256"});
    } else {
      return options.setCipherSuites({"ECDHE-ECDSA-AES128-GCM-SHA256"});
      ;
    }
  }

  ClientSslTransportOptions ecdsaP384OnlyClientOptions() {
    auto options = ClientSslTransportOptions();
    if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
      return options.setSigningAlgorithms({"ecdsa_secp384r1_sha384", "rsa_pss_rsae_sha256"});
    } else {
      return options
          .setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256", "ECDHE-ECDSA-AES128-GCM-SHA256"})
          .setSigningAlgorithms({"rsa_pss_rsae_sha256", "ecdsa_secp384r1_sha384"})
          .setCurves({"P-384"});
    }
  }

  ClientSslTransportOptions ecdsaP256P384OnlyClientOptions() {
    auto options = ClientSslTransportOptions();
    if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
      return options.setSigningAlgorithms(
          {"ecdsa_secp256r1_sha256", "ecdsa_secp384r1_sha384", "rsa_pss_rsae_sha256"});
    } else {
      return options
          .setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256", "ECDHE-ECDSA-AES128-GCM-SHA256"})
          .setSigningAlgorithms(
              {"rsa_pss_rsae_sha256", "ecdsa_secp256r1_sha256", "ecdsa_secp384r1_sha384"})
          .setCurves({"P-256", "P-384"});
    }
  }

  ClientSslTransportOptions ecdsaP256P521OnlyClientOptions() {
    auto options = ClientSslTransportOptions();
    if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
      return options.setSigningAlgorithms(
          {"ecdsa_secp256r1_sha256", "ecdsa_secp521r1_sha512", "rsa_pss_rsae_sha256"});
    } else {
      return options
          .setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256", "ECDHE-ECDSA-AES128-GCM-SHA256"})
          .setSigningAlgorithms(
              {"rsa_pss_rsae_sha256", "ecdsa_secp256r1_sha256", "ecdsa_secp521r1_sha512"})
          .setCurves({"P-256", "P-521"});
    }
  }

  ClientSslTransportOptions ecdsaAllCurvesClientOptions() {
    auto options = ClientSslTransportOptions().setClientEcdsaCert(true);
    if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
      return options.setSigningAlgorithms({"ecdsa_secp256r1_sha256", "ecdsa_secp384r1_sha384",
                                           "ecdsa_secp521r1_sha512", "rsa_pss_rsae_sha256"});
    } else {
      return options
          .setCipherSuites({"ECDHE-ECDSA-AES128-GCM-SHA256", "ECDHE-ECDSA-AES128-GCM-SHA256"})
          .setCurves({"P-256", "P-384", "P-521"});
    }
  }

  static std::string ipClientVersionTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<Network::Address::IpVersion,
                     envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol>>&
          params) {
    return fmt::format("{}_TLSv1_{}", TestUtility::ipVersionToString(std::get<0>(params.param)),
                       std::get<1>(params.param) - 1);
  }

  const envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol tls_version_{
      std::get<1>(GetParam())};
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientVersions, SslCertficateIntegrationTest,
    testing::Combine(
        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
        testing::Values(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2,
                        envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3)),
    SslCertficateIntegrationTest::ipClientVersionTestParamsToString);

// Server with an RSA certificate and a client with RSA/ECDSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerRsa) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = false;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server with an ECDSA certificate and a client with RSA/ECDSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerEcdsa) {
  server_rsa_cert_ = false;
  server_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server with RSA/`ECDSAs` certificates and a client with RSA/ECDSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerRsaEcdsa) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server with an RSA certificate and a client with only RSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ClientRsaOnly) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = false;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(rsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has only an ECDSA certificate, client is only RSA capable, leads to a connection fail.
TEST_P(SslCertficateIntegrationTest, ServerEcdsaClientRsaOnly) {
  server_rsa_cert_ = false;
  server_ecdsa_cert_ = true;
  initialize();
  auto codec_client =
      makeRawHttpConnection(makeSslClientConnection(rsaOnlyClientOptions()), absl::nullopt);
  EXPECT_FALSE(codec_client->connected());
  const std::string counter_name = listenerStatPrefix("ssl.connection_error");
  Stats::CounterSharedPtr counter = test_server_->counter(counter_name);
  test_server_->waitForCounterGe(counter_name, 1);
  EXPECT_EQ(1U, counter->value());
  counter->reset();
}

// Server has only an ECDSA certificate, client is only RSA capable, leads to a connection fail.
// Test the access log.
TEST_P(SslCertficateIntegrationTest, ServerEcdsaClientRsaOnlyWithAccessLog) {
  TestScopedRuntime scoped_runtime;
  useListenerAccessLog("DOWNSTREAM_TRANSPORT_FAILURE_REASON=%DOWNSTREAM_TRANSPORT_FAILURE_REASON% "
                       "FILTER_CHAIN_NAME=%FILTER_CHAIN_NAME%");
  server_rsa_cert_ = false;
  server_ecdsa_cert_ = true;
  initialize();
  auto codec_client =
      makeRawHttpConnection(makeSslClientConnection(rsaOnlyClientOptions()), absl::nullopt);
  EXPECT_FALSE(codec_client->connected());

  auto log_result = waitForAccessLog(listener_access_log_name_);
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
    EXPECT_EQ(log_result,
              "DOWNSTREAM_TRANSPORT_FAILURE_REASON=TLS_error:|268435709:SSL_routines:"
              "OPENSSL_internal:NO_COMMON_SIGNATURE_ALGORITHMS:TLS_error_end FILTER_CHAIN_NAME=-");
  } else {
    EXPECT_EQ(log_result,
              "DOWNSTREAM_TRANSPORT_FAILURE_REASON=TLS_error:|268435640:"
              "SSL_routines:OPENSSL_internal:NO_SHARED_CIPHER:TLS_error_end FILTER_CHAIN_NAME=-");
  }
}

// Server with RSA/ECDSA certificates and a client with only RSA cipher suites works.
// Test empty access log with successful connection.
TEST_P(SslCertficateIntegrationTest, ServerRsaEcdsaClientRsaOnlyWithAccessLog) {
  useListenerAccessLog("DOWNSTREAM_TRANSPORT_FAILURE_REASON=%DOWNSTREAM_TRANSPORT_FAILURE_REASON% "
                       "FILTER_CHAIN_NAME=%FILTER_CHAIN_NAME%");
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(rsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
  codec_client_->close();
  auto log_result = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_result, StartsWith("DOWNSTREAM_TRANSPORT_FAILURE_REASON=- FILTER_CHAIN_NAME=-"));
}

// Server with RSA/ECDSA certificates and a client with only RSA cipher suites works.
TEST_P(SslCertficateIntegrationTest, ServerRsaEcdsaClientRsaOnly) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(rsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has only an RSA certificate, client is only ECDSA capable, leads to connection fail.
TEST_P(SslCertficateIntegrationTest, ServerRsaClientEcdsaOnly) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = false;
  client_ecdsa_cert_ = true;
  initialize();
  EXPECT_FALSE(
      makeRawHttpConnection(makeSslClientConnection(ecdsaOnlyClientOptions()), absl::nullopt)
          ->connected());
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
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
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
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has RSA and ECDSA P-256 certificates, client only supports
// P-384 curve. We fall back to RSA certificate.
TEST_P(SslCertficateIntegrationTest, ServerRsaServerEcdsaP256EcdsaClientEcdsaP384Only) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  // We have to set the server curves to allow for ECDH negotiation where client
  // doesn't support P-256
  server_curves_.push_back("P-256");
  server_curves_.push_back("P-384");
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaP384OnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has RSA and ECDSA P-384 certificates, client only supports
// and P-256 and P-521 curves. We fall back to RSA certificate.
TEST_P(SslCertficateIntegrationTest, ServerRsaServerEcdsaP384EcdsaClientEcdsaP256P521Only) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  server_ecdsa_cert_name_ = "server_ecdsa_p384";
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaP256P521OnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has RSA and ECDSA P-256 certificates, client only supports
// and P-256 and P-384 curves. We fall back to RSA certificate.
TEST_P(SslCertficateIntegrationTest, ServerRsaServerEcdsaP521EcdsaClientEcdsaP256P384Only) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  server_ecdsa_cert_name_ = "server_ecdsa_p521";
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaP256P384OnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has RSA and ECDSA P-384 certificates, client supports all curves.
// We use the ECDSA certificate.
TEST_P(SslCertficateIntegrationTest, ServerRsaServerEcdsaP384EcdsaClientAllCurves) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  server_ecdsa_cert_name_ = "server_ecdsa_p384";
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaAllCurvesClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  for (const Stats::CounterSharedPtr& counter : test_server_->counters()) {
    // Useful for debugging when the test is failing.
    if (counter->name().find("ssl") != std::string::npos) {
      ENVOY_LOG_MISC(critical, "Found ssl metric: {}", counter->name());
    }
  }
  checkStats();
}

// Server has RSA and ECDSA P-521 certificates, client supports all curves.
// We use the ECDSA certificate.
TEST_P(SslCertficateIntegrationTest, ServerRsaServerEcdsaP521EcdsaClientAllCurves) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  server_ecdsa_cert_name_ = "server_ecdsa_p521";
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaAllCurvesClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has an RSA certificate with an OCSP response works.
TEST_P(SslCertficateIntegrationTest, ServerRsaOnlyOcspResponse) {
  server_rsa_cert_ = true;
  server_rsa_cert_ocsp_staple_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(rsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has an ECDSA certificate with an OCSP response works.
TEST_P(SslCertficateIntegrationTest, ServerEcdsaOnlyOcspResponse) {
  server_ecdsa_cert_ = true;
  server_ecdsa_cert_ocsp_staple_ = true;
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has two certificates one with and one without OCSP response works under optional policy.
TEST_P(SslCertficateIntegrationTest, BothEcdsaAndRsaOnlyRsaOcspResponse) {
  server_rsa_cert_ = true;
  server_rsa_cert_ocsp_staple_ = true;
  server_ecdsa_cert_ = true;
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

// Server has two certificates, but only ECDSA has OCSP, which should be returned.
TEST_P(SslCertficateIntegrationTest, BothEcdsaAndRsaOnlyEcdsaOcspResponse) {
  server_rsa_cert_ = true;
  server_ecdsa_cert_ = true;
  server_ecdsa_cert_ocsp_staple_ = true;
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    // Enable OCSP
    auto client = makeSslClientConnection(ecdsaOnlyClientOptions());
    const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
        client->ssl().get());
    SSL_enable_ocsp_stapling(socket->ssl());
    return client;
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
  // Check that there is an OCSP response
  const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      codec_client_->connection()->ssl().get());
  const uint8_t* resp;
  size_t resp_len;
  SSL_get0_ocsp_response(socket->ssl(), &resp, &resp_len);
  ASSERT_GT(resp_len, 0);
  ASSERT_NE(resp, nullptr);
  std::string ocsp_resp{reinterpret_cast<const char*>(resp), resp_len};
  std::string expected_ocsp_resp{TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/server_ecdsa_ocsp_resp.der"))};
  EXPECT_EQ(ocsp_resp, expected_ocsp_resp);
}

// Server has ECDSA and RSA certificates with OCSP responses and stapling required policy works.
TEST_P(SslCertficateIntegrationTest, BothEcdsaAndRsaWithOcspResponseStaplingRequired) {
  server_rsa_cert_ = true;
  server_rsa_cert_ocsp_staple_ = true;
  server_ecdsa_cert_ = true;
  server_ecdsa_cert_ocsp_staple_ = true;
  ocsp_staple_required_ = true;
  client_ecdsa_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection(ecdsaOnlyClientOptions());
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
}

class SslKeyLogTest : public SslIntegrationTest {
public:
  void setLogPath() {
    keylog_path_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  }
  void setLocalFilter() {
    keylog_local_ = true;
    keylog_remote_ = false;
    keylog_local_negative_ = false;
    keylog_remote_negative_ = false;
  }
  void setRemoteFilter() {
    keylog_remote_ = true;
    keylog_local_ = false;
    keylog_local_negative_ = false;
    keylog_remote_negative_ = false;
  }
  void setBothLocalAndRemoteFilter() {
    keylog_local_ = true;
    keylog_remote_ = true;
    keylog_local_negative_ = false;
    keylog_remote_negative_ = false;
  }
  void setNeitherLocalNorRemoteFilter() {
    keylog_remote_ = false;
    keylog_local_ = false;
    keylog_local_negative_ = false;
    keylog_remote_negative_ = false;
  }
  void setNegative() {
    keylog_local_ = true;
    keylog_remote_ = true;
    keylog_local_negative_ = true;
    keylog_remote_negative_ = true;
  }
  void setLocalNegative() {
    keylog_local_ = true;
    keylog_remote_ = true;
    keylog_local_negative_ = false;
    keylog_remote_negative_ = true;
  }
  void setRemoteNegative() {
    keylog_local_ = true;
    keylog_remote_ = true;
    keylog_local_negative_ = true;
    keylog_remote_negative_ = false;
  }
  void setMultipleIps() {
    keylog_local_ = true;
    keylog_remote_ = true;
    keylog_local_negative_ = false;
    keylog_remote_negative_ = false;
    keylog_multiple_ips_ = true;
  }
  void logCheck() {
    EXPECT_TRUE(api_->fileSystem().fileExists(keylog_path_));
    std::string log = waitForAccessLog(keylog_path_);
    if (server_tlsv1_3_) {
      /** The key log for TLS1.3 is as follows:
       * CLIENT_HANDSHAKE_TRAFFIC_SECRET
         c62fe86cb3a714451abc7496062251e16862ca3dfc1487c97ab4b291b83a1787
         b335f2ce9079d824a7d2f5ef9af6572d43942d6803bac1ae9de1e840c15c993ae4efdf4ac087877031d1936d5bb858e3
         SERVER_HANDSHAKE_TRAFFIC_SECRET
         c62fe86cb3a714451abc7496062251e16862ca3dfc1487c97ab4b291b83a1787
         f498c03446c936d8a17f31669dd54cee2d9bc8d5b7e1a658f677b5cd6e0965111c2331fcc337c01895ec9a0ed12be34a
         CLIENT_TRAFFIC_SECRET_0 c62fe86cb3a714451abc7496062251e16862ca3dfc1487c97ab4b291b83a1787
         0bbbb2056f3d35a3b610c5cc8ae0b9b63a120912ff25054ee52b853fefc59e12e9fdfebc409347c737394457bfd36bde
         SERVER_TRAFFIC_SECRET_0 c62fe86cb3a714451abc7496062251e16862ca3dfc1487c97ab4b291b83a1787
         bd3e1757174d82c308515a0c02b981084edda53e546df551ddcf78043bff831c07ff93c7ab3e8ef9e2206c8319c25331
         EXPORTER_SECRET c62fe86cb3a714451abc7496062251e16862ca3dfc1487c97ab4b291b83a1787
         6bd19fbdd12e6710159bcb406fd42a580c41236e2d53072dba3064f9b3ff214662081f023e9b22325e31fee5bb11b172
       */
      EXPECT_THAT(log, testing::HasSubstr("CLIENT_TRAFFIC_SECRET"));
      EXPECT_THAT(log, testing::HasSubstr("SERVER_TRAFFIC_SECRET"));
      EXPECT_THAT(log, testing::HasSubstr("CLIENT_HANDSHAKE_TRAFFIC_SECRET"));
      EXPECT_THAT(log, testing::HasSubstr("SERVER_HANDSHAKE_TRAFFIC_SECRET"));
      EXPECT_THAT(log, testing::HasSubstr("EXPORTER_SECRET"));
    } else {
      /** The key log for TLS1.1/1.2 is as follows:
       * CLIENT_RANDOM 5a479a50fe3e85295840b84e298aeb184cecc34ced22d963e16b01dc48c9530f
         d6840f8100e4ceeb282946cdd72fe403b8d0724ee816ab2d0824b6d6b5033d333ec4b2e77f515226f5d829e137855ef1
       */
      EXPECT_THAT(log, testing::HasSubstr("CLIENT_RANDOM"));
    }
  }
  void negativeCheck() {
    EXPECT_TRUE(api_->fileSystem().fileExists(keylog_path_));
    auto size = api_->fileSystem().fileSize(keylog_path_);
    EXPECT_EQ(size, 0);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslKeyLogTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SslKeyLogTest, SetLocalFilter) {
  setLogPath();
  setLocalFilter();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  logCheck();
}

TEST_P(SslKeyLogTest, SetRemoteFilter) {
  setLogPath();
  setRemoteFilter();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  logCheck();
}

TEST_P(SslKeyLogTest, SetLocalAndRemoteFilter) {
  setLogPath();
  setBothLocalAndRemoteFilter();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  logCheck();
}

TEST_P(SslKeyLogTest, SetNeitherLocalNorRemoteFilter) {
  setLogPath();
  setNeitherLocalNorRemoteFilter();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  logCheck();
}

TEST_P(SslKeyLogTest, SetLocalAndRemoteFilterNegative) {
  setLogPath();
  setNegative();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  negativeCheck();
}

TEST_P(SslKeyLogTest, SetLocalNegative) {
  setLogPath();
  setLocalNegative();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  negativeCheck();
}

TEST_P(SslKeyLogTest, SetRemoteNegative) {
  setLogPath();
  setRemoteNegative();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  negativeCheck();
}

TEST_P(SslKeyLogTest, SetMultipleIps) {
  setLogPath();
  setMultipleIps();
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto result = codec_client_->startRequest(request_headers);
  codec_client_->close();
  logCheck();
}

TEST_P(SslIntegrationTest, SyncCertSelectorSucceeds) {
  tls_cert_selector_yaml_ = R"EOF(
name: test-tls-context-provider
typed_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: sync
  )EOF";
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, false, &creator);
  checkStats();
  EXPECT_EQ(test_server_->counter("aysnc_cert_selection.cert_selection_sync")->value(), 1);
}

TEST_P(SslIntegrationTest, AsyncCertSelectorSucceeds) {
  tls_cert_selector_yaml_ = R"EOF(
name: test-tls-context-provider
typed_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: async
  )EOF";
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, false, &creator);
  checkStats();

  EXPECT_EQ(test_server_->counter("aysnc_cert_selection.cert_selection_async")->value(), 1);
  EXPECT_EQ(test_server_->counter("aysnc_cert_selection.cert_selection_async_finished")->value(),
            1);
}

TEST_P(SslIntegrationTest, AsyncSleepCertSelectorSucceeds) {
  tls_cert_selector_yaml_ = R"EOF(
name: test-tls-context-provider
typed_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: sleep
  )EOF";
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, false, &creator);
  checkStats();

  EXPECT_EQ(test_server_->counter("aysnc_cert_selection.cert_selection_sleep")->value(), 1);
  EXPECT_EQ(test_server_->counter("aysnc_cert_selection.cert_selection_sleep_finished")->value(),
            1);
}

TEST_P(SslIntegrationTest, AsyncSleepCertSelectionAfterTearDown) {
  tls_cert_selector_yaml_ = R"EOF(
name: test-tls-context-provider
typed_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: sleep
  )EOF";
  initialize();

  Network::ClientConnectionPtr connection = makeSslClientConnection({});
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      connection->ssl().get());
  ASSERT(socket);

  // wait for the server tls handshake into sleep state.
  test_server_->waitForCounterEq("aysnc_cert_selection.cert_selection_sleep", 1,
                                 TestUtility::DefaultTimeout, dispatcher_.get());

  ASSERT_EQ(connection->state(), Network::Connection::State::Open);
  ENVOY_LOG_MISC(debug, "debug: closing connection");
  connection->close(Network::ConnectionCloseType::NoFlush);
  connection.reset();

  // wait the sleep timer in cert selector is triggered.
  test_server_->waitForCounterEq("aysnc_cert_selection.cert_selection_sleep_finished", 1,
                                 TestUtility::DefaultTimeout, dispatcher_.get());
}

TEST_P(SslIntegrationTest, AsyncCertSelectionAfterSslShutdown) {
  tls_cert_selector_yaml_ = R"EOF(
name: test-tls-context-provider
typed_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: sleep
  )EOF";
  initialize();

  Network::ClientConnectionPtr connection = makeSslClientConnection({});
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  const auto* socket = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      connection->ssl().get());
  ASSERT(socket);

  // wait for the server tls handshake into sleep state.
  test_server_->waitForCounterEq("aysnc_cert_selection.cert_selection_sleep", 1,
                                 TestUtility::DefaultTimeout, dispatcher_.get());

  ASSERT_EQ(connection->state(), Network::Connection::State::Open);
  connection->close(Network::ConnectionCloseType::NoFlush);

  // wait the sleep timer in cert selector is triggered.
  test_server_->waitForCounterEq("aysnc_cert_selection.cert_selection_sleep_finished", 1,
                                 TestUtility::DefaultTimeout, dispatcher_.get());

  connection.reset();
}

} // namespace Ssl
} // namespace Envoy
