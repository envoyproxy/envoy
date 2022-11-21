#include "ssl_integration_test.h"

#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_handshaker.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/extensions/transport_sockets/tls/cert_validator/timed_cert_validator.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifdef ENVOY_ADMIN_FUNCTIONALITY
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"
#include "envoy/extensions/transport_sockets/tap/v3/tap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "test/extensions/common/tap/common.h"
#endif

namespace Envoy {

using Extensions::TransportSockets::Tls::ContextImplPeer;

namespace Ssl {

void SslIntegrationTestBase::initialize() {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(server_rsa_cert_)
                                  .setRsaCertOcspStaple(server_rsa_cert_ocsp_staple_)
                                  .setEcdsaCert(server_ecdsa_cert_)
                                  .setEcdsaCertOcspStaple(server_ecdsa_cert_ocsp_staple_)
                                  .setOcspStapleRequired(ocsp_staple_required_)
                                  .setTlsV13(server_tlsv1_3_)
                                  .setExpectClientEcdsaCert(client_ecdsa_cert_)
                                  .setTlsKeyLogFilter(keylog_local_, keylog_remote_,
                                                      keylog_local_negative_,
                                                      keylog_remote_negative_, keylog_path_,
                                                      keylog_multiple_ips_, version_));

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
      client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
}

void SslIntegrationTestBase::checkStats() {
  const uint32_t expected_handshakes = debug_with_s_client_ ? 2 : 1;
  Stats::CounterSharedPtr counter = test_server_->counter(listenerStatPrefix("ssl.handshake"));
  EXPECT_EQ(expected_handshakes, counter->value());
  counter->reset();
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

TEST_P(SslIntegrationTest, AsyncCertValidationSucceeds) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  // Config client to use an async cert validator which defer the actual validation by 5ms.
  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config =
      new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  initialize();

  Network::ClientConnectionPtr connection = makeSslClientConnection(
      ClientSslTransportOptions().setCustomCertValidatorConfig(custom_validator_config));
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

TEST_P(SslIntegrationTest, AsyncCertValidationAfterTearDown) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config =
      new envoy::config::core::v3::TypedExtensionConfig();
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
      ClientSslTransportOptions().setCustomCertValidatorConfig(custom_validator_config),
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
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config =
      new envoy::config::core::v3::TypedExtensionConfig();
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
      ClientSslTransportOptions().setCustomCertValidatorConfig(custom_validator_config),
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
      return ClientSslTransportOptions().setSigningAlgorithmsForTest("rsa_pss_rsae_sha256");
    } else {
      return ClientSslTransportOptions().setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
    }
  }

  ClientSslTransportOptions ecdsaOnlyClientOptions() {
    auto options = ClientSslTransportOptions().setClientEcdsaCert(true);
    if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3) {
      return options.setSigningAlgorithmsForTest("ecdsa_secp256r1_sha256");
    } else {
      return options.setCipherSuites({"ECDHE-ECDSA-AES128-GCM-SHA256"});
    }
  }

  static std::string ipClientVersionTestParamsToString(
      const ::testing::TestParamInfo<
          std::tuple<Network::Address::IpVersion,
                     envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol>>&
          params) {
    return fmt::format("{}_TLSv1_{}",
                       std::get<0>(params.param) == Network::Address::IpVersion::v4 ? "IPv4"
                                                                                    : "IPv6",
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
  EXPECT_NE(0, resp_len);
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

#ifdef ENVOY_ADMIN_FUNCTIONALITY
// TODO(zuercher): write an additional OCSP integration test that validates behavior with an
// expired OCSP response. (Requires OCSP client-side support in upstream TLS.)

// TODO(mattklein123): Move this into a dedicated integration test for the tap transport socket as
// well as add more tests.
class SslTapIntegrationTest : public SslIntegrationTest {
public:
  void initialize() override {
    // TODO(mattklein123): Merge/use the code in ConfigHelper::setTapTransportSocket().
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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

  void setupUpstreamTap(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* transport_socket =
        bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
    transport_socket->set_name("envoy.transport_sockets.tap");
    envoy::config::core::v3::TransportSocket raw_transport_socket;
    raw_transport_socket.set_name("envoy.transport_sockets.raw_buffer");
    envoy::extensions::transport_sockets::tap::v3::Tap tap_config =
        createTapConfig(raw_transport_socket);
    tap_config.mutable_transport_socket()->MergeFrom(raw_transport_socket);
    transport_socket->mutable_typed_config()->PackFrom(tap_config);
  }

  void setupDownstreamTap(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    // Configure inner SSL transport socket based on existing config.
    envoy::config::core::v3::TransportSocket ssl_transport_socket;
    auto* transport_socket = filter_chain->mutable_transport_socket();
    ssl_transport_socket.Swap(transport_socket);
    // Configure outer tap transport socket.
    transport_socket->set_name("envoy.transport_sockets.tap");
    envoy::extensions::transport_sockets::tap::v3::Tap tap_config =
        createTapConfig(ssl_transport_socket);
    tap_config.mutable_transport_socket()->MergeFrom(ssl_transport_socket);
    transport_socket->mutable_typed_config()->PackFrom(tap_config);
  }

  envoy::extensions::transport_sockets::tap::v3::Tap
  createTapConfig(const envoy::config::core::v3::TransportSocket& inner_transport) {
    envoy::extensions::transport_sockets::tap::v3::Tap tap_config;
    tap_config.mutable_common_config()->mutable_static_config()->mutable_match()->set_any_match(
        true);
    auto* output_config =
        tap_config.mutable_common_config()->mutable_static_config()->mutable_output_config();
    if (max_rx_bytes_.has_value()) {
      output_config->mutable_max_buffered_rx_bytes()->set_value(max_rx_bytes_.value());
    }
    if (max_tx_bytes_.has_value()) {
      output_config->mutable_max_buffered_tx_bytes()->set_value(max_tx_bytes_.value());
    }
    output_config->set_streaming(streaming_tap_);

    auto* output_sink = output_config->mutable_sinks()->Add();
    output_sink->set_format(format_);
    output_sink->mutable_file_per_tap()->set_path_prefix(path_prefix_);
    tap_config.mutable_transport_socket()->MergeFrom(inner_transport);
    return tap_config;
  }

  std::string path_prefix_ = TestEnvironment::temporaryPath("ssl_trace");
  envoy::config::tap::v3::OutputSink::Format format_{
      envoy::config::tap::v3::OutputSink::PROTO_BINARY};
  absl::optional<uint64_t> max_rx_bytes_;
  absl::optional<uint64_t> max_tx_bytes_;
  bool upstream_tap_{};
  bool streaming_tap_{};
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
  Http::TestRequestHeaderMapImpl post_request_headers{
      {":method", "POST"},       {":path", "/test/long/url"},
      {":scheme", "http"},       {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
  auto response =
      sendRequestAndWaitForResponse(post_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(256, response->body().size());
  checkStats();
  envoy::config::core::v3::Address expected_local_address;
  Network::Utility::addressToProtobufAddress(
      *codec_client_->connection()->connectionInfoProvider().remoteAddress(),
      expected_local_address);
  envoy::config::core::v3::Address expected_remote_address;
  Network::Utility::addressToProtobufAddress(
      *codec_client_->connection()->connectionInfoProvider().localAddress(),
      expected_remote_address);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::data::tap::v3::TraceWrapper trace;
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
  Http::TestRequestHeaderMapImpl get_request_headers{
      {":method", "GET"},        {":path", "/test/long/url"},
      {":scheme", "http"},       {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
  response =
      sendRequestAndWaitForResponse(get_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
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
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "sni.lyft.com"}};
  auto result = codec_client_->startRequest(request_headers);
  auto response = std::move(result.second);
  Buffer::OwnedImpl data1("one");
  result.first.encodeData(data1, false);
  Buffer::OwnedImpl data2("two");
  result.first.encodeData(data2, true);
  waitForNextUpstreamRequest();
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl data3("three");
  upstream_request_->encodeData(data3, false);
  response->waitForBodyData(5);
  Buffer::OwnedImpl data4("four");
  upstream_request_->encodeData(data4, true);
  ASSERT_TRUE(response->waitForEndStream());

  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  envoy::data::tap::v3::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, id), trace, *api_);

  ASSERT_EQ(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(trace.socket_buffered_trace().events(0).read().data().truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().events(1).write().data().truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().write_truncated());
}

// Validate a single request with text proto output.
TEST_P(SslTapIntegrationTest, RequestWithTextProto) {
  format_ = envoy::config::tap::v3::OutputSink::PROTO_TEXT;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // Disable for this test because it uses connection IDs, which disrupts the accounting below
  // leading to the wrong path for the `pb_text` being used.
  skip_tag_extraction_rule_check_ = true;

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::data::tap::v3::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb_text", path_prefix_, id), trace, *api_);
  // Test some obvious properties.
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data().as_bytes(),
                               "GET /test/long/url HTTP/1.1"));
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

  format_ = envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // Disable for this test because it uses connection IDs, which disrupts the accounting below
  // leading to the wrong path for the `pb_text` being used.
  skip_tag_extraction_rule_check_ = true;

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 2;
  testRouterRequestAndResponseWithBody(512, 1024, false, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  test_server_.reset();

  // This must be done after server shutdown so that connection pool connections are closed and
  // the tap written.
  envoy::data::tap::v3::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.json", path_prefix_, id), trace, *api_);

  // Test some obvious properties.
  EXPECT_EQ(trace.socket_buffered_trace().events(0).write().data().as_string(), "GET ");
  EXPECT_EQ(trace.socket_buffered_trace().events(1).read().data().as_string(), "HTTP/");
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().write_truncated());
}

// Validate a single request with length delimited binary proto output. This test uses an upstream
// tap.
TEST_P(SslTapIntegrationTest, RequestWithStreamingUpstreamTap) {
  upstream_tap_ = true;
  streaming_tap_ = true;
  max_rx_bytes_ = 5;
  max_tx_bytes_ = 4;

  format_ = envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // Disable for this test because it uses connection IDs, which disrupts the accounting below
  // leading to the wrong path for the `pb_text` being used.
  skip_tag_extraction_rule_check_ = true;

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 2;
  testRouterRequestAndResponseWithBody(512, 1024, false, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  test_server_.reset();

  // This must be done after server shutdown so that connection pool connections are closed and
  // the tap written.
  std::vector<envoy::data::tap::v3::TraceWrapper> traces =
      Extensions::Common::Tap::readTracesFromFile(
          fmt::format("{}_{}.pb_length_delimited", path_prefix_, id));
  ASSERT_GE(traces.size(), 4);

  // The initial connection message has no local address, but has a remote address (not connected
  // yet).
  EXPECT_TRUE(traces[0].socket_streamed_trace_segment().has_connection());
  EXPECT_FALSE(traces[0].socket_streamed_trace_segment().connection().has_local_address());
  EXPECT_TRUE(traces[0].socket_streamed_trace_segment().connection().has_remote_address());

  // Verify truncated request/response data.
  EXPECT_EQ(traces[1].socket_streamed_trace_segment().event().write().data().as_bytes(), "GET ");
  EXPECT_TRUE(traces[1].socket_streamed_trace_segment().event().write().data().truncated());
  EXPECT_EQ(traces[2].socket_streamed_trace_segment().event().read().data().as_bytes(), "HTTP/");
  EXPECT_TRUE(traces[2].socket_streamed_trace_segment().event().read().data().truncated());
}
#endif

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

} // namespace Ssl
} // namespace Envoy
