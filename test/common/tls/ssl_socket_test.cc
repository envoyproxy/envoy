#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/tcp_listener_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/private_key/private_key_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/common/tls/cert_validator/timed_cert_validator.h"
#include "test/common/tls/ssl_certs_test.h"
#include "test/common/tls/test_data/ca_cert_info.h"
#include "test/common/tls/test_data/extensions_cert_info.h"
#include "test/common/tls/test_data/intermediate_ca_cert_info.h"
#include "test/common/tls/test_data/no_san_cert_info.h"
#include "test/common/tls/test_data/password_protected_cert_info.h"
#include "test/common/tls/test_data/san_dns2_cert_info.h"
#include "test/common/tls/test_data/san_dns3_cert_info.h"
#include "test/common/tls/test_data/san_dns4_cert_info.h"
#include "test/common/tls/test_data/san_dns_cert_info.h"
#include "test/common/tls/test_data/san_dns_ecdsa_1_cert_info.h"
#include "test/common/tls/test_data/san_dns_rsa_1_cert_info.h"
#include "test/common/tls/test_data/san_dns_rsa_2_cert_info.h"
#include "test/common/tls/test_data/san_multiple_dns_1_cert_info.h"
#include "test/common/tls/test_data/san_multiple_dns_cert_info.h"
#include "test/common/tls/test_data/san_uri_cert_info.h"
#include "test/common/tls/test_data/selfsigned_ecdsa_p256_cert_info.h"
#include "test/common/tls/test_private_key_method_provider.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::ContainsRegex;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace {

/**
 * A base class to hold the options for testUtil() and testUtilV2().
 */
class TestUtilOptionsBase {
public:
  const std::vector<std::string>& expectedClientCertUri() const {
    return expected_client_cert_uri_;
  }
  const std::vector<std::string>& expectedClientCertIpSans() const {
    return expected_client_cert_ip_sans_;
  }
  const std::string& expectedServerStats() const { return expected_server_stats_; }
  bool expectSuccess() const { return expect_success_; }
  Network::Address::IpVersion version() const { return version_; }

protected:
  TestUtilOptionsBase(bool expect_success, Network::Address::IpVersion version)
      : expect_success_(expect_success), version_(version) {}

  void setExpectedClientCertUri(const std::string& expected_client_cert_uri) {
    expected_client_cert_uri_ = {expected_client_cert_uri};
  }

  void setExpectedClientIpSans(const std::vector<std::string>& expected_client_ip_sans) {
    expected_client_cert_ip_sans_ = expected_client_ip_sans;
  }

  void setExpectedServerStats(const std::string& expected_server_stats) {
    expected_server_stats_ = expected_server_stats;
  }

private:
  const bool expect_success_;
  const Network::Address::IpVersion version_;

  std::string expected_server_stats_;
  std::vector<std::string> expected_client_cert_uri_;
  std::vector<std::string> expected_client_cert_ip_sans_;
};

/**
 * A class to hold the options for testUtil().
 */
class TestUtilOptions : public TestUtilOptionsBase {
public:
  TestUtilOptions(const std::string& client_ctx_yaml, const std::string& server_ctx_yaml,
                  bool expect_success, Network::Address::IpVersion version)
      : TestUtilOptionsBase(expect_success, version), client_ctx_yaml_(client_ctx_yaml),
        server_ctx_yaml_(server_ctx_yaml) {
    if (expect_success) {
      setExpectedServerStats("ssl.handshake");
    } else {
      setExpectedServerStats("ssl.fail_verify_error");
    }
  }

  const std::string& clientCtxYaml() const { return client_ctx_yaml_; }
  const std::string& serverCtxYaml() const { return server_ctx_yaml_; }

  TestUtilOptions& setExpectedServerStats(const std::string& expected_server_stats) {
    TestUtilOptionsBase::setExpectedServerStats(expected_server_stats);
    return *this;
  }

  bool expectNoCert() const { return expect_no_cert_; }

  TestUtilOptions& setExpectNoCert() {
    expect_no_cert_ = true;
    return *this;
  }

  bool expectNoCertChain() const { return expect_no_cert_chain_; }

  TestUtilOptions& setExpectNoCertChain() {
    expect_no_cert_chain_ = true;
    return *this;
  }

  TestUtilOptions& setExpectedClientCertUri(const std::string& expected_client_cert_uri) {
    TestUtilOptionsBase::setExpectedClientCertUri(expected_client_cert_uri);
    return *this;
  }

  TestUtilOptions& setExpectedSha256Digest(const std::string& expected_sha256_digest) {
    expected_sha256_digest_ = expected_sha256_digest;
    return *this;
  }

  const std::string& expectedSha256Digest() const { return expected_sha256_digest_; }

  TestUtilOptions& setExpectedSha256Digests(std::vector<std::string>& expected_sha256_digests) {
    expected_sha256_digests_ = expected_sha256_digests;
    return *this;
  }
  const std::vector<std::string> expectedSha256Digests() const { return expected_sha256_digests_; }

  TestUtilOptions& setExpectedSha1Digest(const std::string& expected_sha1_digest) {
    expected_sha1_digest_ = expected_sha1_digest;
    return *this;
  }

  const std::string& expectedSha1Digest() const { return expected_sha1_digest_; }

  TestUtilOptions& setExpectedSha1Digests(std::vector<std::string>& expected_sha1_digests) {
    expected_sha1_digests_ = expected_sha1_digests;
    return *this;
  }

  const std::vector<std::string> expectedSha1Digests() const { return expected_sha1_digests_; }

  TestUtilOptions& setExpectedLocalUri(const std::string& expected_local_uri) {
    expected_local_uri_ = {expected_local_uri};
    return *this;
  }

  const std::vector<std::string>& expectedLocalUri() const { return expected_local_uri_; }

  TestUtilOptions& setExpectedLocalDns(const std::string& expected_local_dns) {
    expected_local_dns_ = {expected_local_dns};
    return *this;
  }

  const std::vector<std::string>& expectedLocalDns() const { return expected_local_dns_; }

  TestUtilOptions& setExpectedLocalIp(const std::string& expected_local_ip) {
    expected_local_ip_ = {expected_local_ip};
    return *this;
  }

  const std::vector<std::string>& expectedLocalIp() const { return expected_local_ip_; }

  TestUtilOptions& setExpectedSerialNumber(const std::string& expected_serial_number) {
    expected_serial_number_ = expected_serial_number;
    return *this;
  }

  const std::string& expectedSerialNumber() const { return expected_serial_number_; }

  TestUtilOptions& setExpectedSerialNumbers(std::vector<std::string>& expected_serial_numbers) {
    expected_serial_numbers_ = expected_serial_numbers;
    return *this;
  }

  const std::vector<std::string> expectedSerialNumbers() const { return expected_serial_numbers_; }

  TestUtilOptions& setExpectedPeerIssuer(const std::string& expected_peer_issuer) {
    expected_peer_issuer_ = expected_peer_issuer;
    return *this;
  }

  const std::string& expectedPeerIssuer() const { return expected_peer_issuer_; }

  TestUtilOptions& setExpectedPeerSubject(const std::string& expected_peer_subject) {
    expected_peer_subject_ = expected_peer_subject;
    return *this;
  }

  const std::string& expectedPeerSubject() const { return expected_peer_subject_; }

  TestUtilOptions& setExpectedLocalSubject(const std::string& expected_local_subject) {
    expected_local_subject_ = expected_local_subject;
    return *this;
  }

  const std::string& expectedLocalSubject() const { return expected_local_subject_; }

  TestUtilOptions& setExpectedPeerOids(const std::vector<std::string>& expected_peer_oids) {
    expected_peer_oids_ = expected_peer_oids;
    return *this;
  }

  const std::vector<std::string>& expectedPeerOids() const { return expected_peer_oids_; }

  TestUtilOptions& setExpectedLocalOids(const std::vector<std::string>& expected_local_oids) {
    expected_local_oids_ = expected_local_oids;
    return *this;
  }

  const std::vector<std::string>& expectedLocalOids() const { return expected_local_oids_; }

  TestUtilOptions& setExpectedPeerCert(const std::string& expected_peer_cert) {
    expected_peer_cert_ = expected_peer_cert;
    return *this;
  }

  const std::string& expectedPeerCert() const { return expected_peer_cert_; }

  TestUtilOptions& setExpectedPeerCertChain(const std::string& expected_peer_cert_chain) {
    expected_peer_cert_chain_ = expected_peer_cert_chain;
    return *this;
  }

  const std::string& expectedPeerCertChain() const { return expected_peer_cert_chain_; }

  TestUtilOptions& setExpectedValidFromTimePeerCert(const std::string& expected_valid_from) {
    expected_valid_from_peer_cert_ = expected_valid_from;
    return *this;
  }

  const std::string& expectedValidFromTimePeerCert() const {
    return expected_valid_from_peer_cert_;
  }

  TestUtilOptions& setExpectedExpirationTimePeerCert(const std::string& expected_expiration) {
    expected_expiration_peer_cert_ = expected_expiration;
    return *this;
  }

  const std::string& expectedExpirationTimePeerCert() const {
    return expected_expiration_peer_cert_;
  }

  TestUtilOptions& setPrivateKeyMethodExpected(bool expected_method) {
    expect_private_key_method_ = expected_method;
    return *this;
  }

  bool expectedPrivateKeyMethod() const { return expect_private_key_method_; }

  TestUtilOptions& setExpectedServerCloseEvent(Network::ConnectionEvent expected_event) {
    expected_server_close_event_ = expected_event;
    return *this;
  }

  Network::ConnectionEvent expectedServerCloseEvent() const { return expected_server_close_event_; }

  TestUtilOptions& setExpectedOcspResponse(const std::string& expected_ocsp_response) {
    expected_ocsp_response_ = expected_ocsp_response;
    return *this;
  }

  const std::string& expectedOcspResponse() const { return expected_ocsp_response_; }

  TestUtilOptions& enableOcspStapling() {
    ocsp_stapling_enabled_ = true;
    return *this;
  }

  bool ocspStaplingEnabled() const { return ocsp_stapling_enabled_; }

  TestUtilOptions& setExpectedTransportFailureReasonContains(
      const std::string& expected_transport_failure_reason_contains) {
    expected_transport_failure_reason_contains_ = expected_transport_failure_reason_contains;
    return *this;
  }

  const std::string& expectedTransportFailureReasonContains() const {
    return expected_transport_failure_reason_contains_;
  }

  TestUtilOptions& setNotExpectedClientStats(const std::string& stat) {
    not_expected_client_stats_ = stat;
    return *this;
  }
  const std::string& notExpectedClientStats() const { return not_expected_client_stats_; }

  TestUtilOptions& setExpectedVerifyErrorCode(int code) {
    expected_verify_error_code_ = code;
    return *this;
  }

  int expectedVerifyErrorCode() const { return expected_verify_error_code_; }

  TestUtilOptions& setExpectedSni(absl::string_view expected_sni) {
    expected_sni_ = std::string(expected_sni);
    return *this;
  }

  const std::string& expectedSni() const { return expected_sni_; }

private:
  const std::string client_ctx_yaml_;
  const std::string server_ctx_yaml_;

  bool expect_no_cert_{false};
  bool expect_no_cert_chain_{false};
  bool expect_private_key_method_{false};
  NiceMock<Runtime::MockLoader> runtime_;
  Network::ConnectionEvent expected_server_close_event_{Network::ConnectionEvent::RemoteClose};
  std::string expected_sha256_digest_;
  std::vector<std::string> expected_sha256_digests_;
  std::string expected_sha1_digest_;
  std::vector<std::string> expected_sha1_digests_;
  std::vector<std::string> expected_local_uri_;
  std::vector<std::string> expected_local_dns_;
  std::vector<std::string> expected_local_ip_;
  std::string expected_serial_number_;
  std::vector<std::string> expected_serial_numbers_;
  std::string expected_peer_issuer_;
  std::string expected_peer_subject_;
  std::string expected_local_subject_;
  std::vector<std::string> expected_peer_oids_;
  std::vector<std::string> expected_local_oids_;
  std::string expected_peer_cert_;
  std::string expected_peer_cert_chain_;
  std::string expected_valid_from_peer_cert_;
  std::string expected_expiration_peer_cert_;
  std::string expected_ocsp_response_;
  bool ocsp_stapling_enabled_{false};
  std::string expected_transport_failure_reason_contains_;
  std::string not_expected_client_stats_;
  int expected_verify_error_code_{-1};
  std::string expected_sni_;
};

Network::ListenerPtr createListener(Network::SocketSharedPtr&& socket,
                                    Network::TcpListenerCallbacks& cb, Runtime::Loader& runtime,
                                    const Network::ListenerConfig& listener_config,
                                    Server::ThreadLocalOverloadStateOptRef overload_state,
                                    Random::RandomGenerator& rng, Event::Dispatcher& dispatcher) {
  return std::make_unique<Network::TcpListenerImpl>(
      dispatcher, rng, runtime, socket, cb, listener_config.bindToPort(),
      listener_config.ignoreGlobalConnLimit(), listener_config.shouldBypassOverloadManager(),
      listener_config.maxConnectionsToAcceptPerSocketEvent(), overload_state);
}

void testUtil(const TestUtilOptions& options) {
  Event::SimulatedTimeSystem time_system;

  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system);
  NiceMock<Runtime::MockLoader> runtime;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      transport_socket_factory_context;
  ON_CALL(transport_socket_factory_context.server_context_, api())
      .WillByDefault(ReturnRef(*server_api));

  // For private key method testing.
  NiceMock<Ssl::MockContextManager> context_manager;
  Extensions::PrivateKeyMethodProvider::TestPrivateKeyMethodFactory test_factory;
  Registry::InjectFactory<Ssl::PrivateKeyMethodProviderInstanceFactory>
      test_private_key_method_factory(test_factory);
  PrivateKeyMethodManagerImpl private_key_method_manager;
  if (options.expectedPrivateKeyMethod()) {
    EXPECT_CALL(transport_socket_factory_context, sslContextManager())
        .WillOnce(ReturnRef(context_manager))
        .WillRepeatedly(ReturnRef(context_manager));
    EXPECT_CALL(context_manager, privateKeyMethodManager())
        .WillOnce(ReturnRef(private_key_method_manager))
        .WillRepeatedly(ReturnRef(private_key_method_manager));
  }

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(options.serverCtxYaml()),
                            server_tls_context);
  auto server_cfg = THROW_OR_RETURN_VALUE(
      ServerContextConfigImpl::create(server_tls_context, transport_socket_factory_context, false),
      std::unique_ptr<ServerContextConfigImpl>);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  Event::DispatcherPtr dispatcher = server_api->allocateDispatcher("test_thread");
  auto server_ssl_socket_factory = THROW_OR_RETURN_VALUE(
      ServerSslSocketFactory::create(std::move(server_cfg), manager,
                                     *server_stats_store.rootScope(), std::vector<std::string>{}),
      std::unique_ptr<ServerSslSocketFactory>);

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(options.version()));
  Network::MockTcpListenerCallbacks callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime, listener_config, overload_state,
                     server_api->randomGenerator(), *dispatcher);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(options.clientCtxYaml()),
                            client_tls_context);

  Stats::TestUtil::TestStore client_stats_store;
  Api::ApiPtr client_api = Api::createApiForTest(client_stats_store, time_system);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      client_factory_context;
  ON_CALL(client_factory_context.server_context_, api()).WillByDefault(ReturnRef(*client_api));

  auto client_cfg = *ClientContextConfigImpl::create(client_tls_context, client_factory_context);
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                                   *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        auto ssl_socket = server_ssl_socket_factory->createDownstreamTransportSocket();
        // configureInitialCongestionWindow is an unimplemented empty function, this is just to
        // increase code coverage.
        ssl_socket->configureInitialCongestionWindow(100, std::chrono::microseconds(123));
        server_connection = dispatcher->createServerConnection(std::move(socket),
                                                               std::move(ssl_socket), stream_info);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  if (options.ocspStaplingEnabled()) {
    const SslHandshakerImpl* ssl_socket =
        dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
    SSL_enable_ocsp_stapling(ssl_socket->ssl());
  }

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  size_t connect_count = 0;
  auto connect_second_time = [&]() {
    if (++connect_count == 2) {
      if (!options.expectedSha256Digest().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedSha256Digest(),
                  server_connection->ssl()->sha256PeerCertificateDigest());
        EXPECT_EQ(options.expectedSha256Digest(),
                  server_connection->ssl()->sha256PeerCertificateDigest());
      }
      if (!options.expectedSha256Digests().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedSha256Digests(),
                  server_connection->ssl()->sha256PeerCertificateChainDigests());
        EXPECT_EQ(options.expectedSha256Digests(),
                  server_connection->ssl()->sha256PeerCertificateChainDigests());
      }
      if (!options.expectedSha1Digest().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedSha1Digest(),
                  server_connection->ssl()->sha1PeerCertificateDigest());
        EXPECT_EQ(options.expectedSha1Digest(),
                  server_connection->ssl()->sha1PeerCertificateDigest());
      }
      if (!options.expectedSha1Digests().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedSha1Digests(),
                  server_connection->ssl()->sha1PeerCertificateChainDigests());
        EXPECT_EQ(options.expectedSha1Digests(),
                  server_connection->ssl()->sha1PeerCertificateChainDigests());
      }
      // Assert twice to ensure a cached value is returned and still valid.
      EXPECT_EQ(options.expectedClientCertUri(), server_connection->ssl()->uriSanPeerCertificate());
      EXPECT_EQ(options.expectedClientCertUri(), server_connection->ssl()->uriSanPeerCertificate());

      if (!options.expectedLocalUri().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedLocalUri(), server_connection->ssl()->uriSanLocalCertificate());
        EXPECT_EQ(options.expectedLocalUri(), server_connection->ssl()->uriSanLocalCertificate());
      }

      if (!options.expectedLocalDns().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedLocalDns(), server_connection->ssl()->dnsSansLocalCertificate());
        EXPECT_EQ(options.expectedLocalDns(), server_connection->ssl()->dnsSansLocalCertificate());
      }

      if (!options.expectedLocalIp().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedLocalIp(), server_connection->ssl()->ipSansLocalCertificate());
        EXPECT_EQ(options.expectedLocalIp(), server_connection->ssl()->ipSansLocalCertificate());
      }

      EXPECT_EQ(options.expectedSerialNumber(),
                server_connection->ssl()->serialNumberPeerCertificate());
      EXPECT_EQ(options.expectedSerialNumber(),
                server_connection->ssl()->serialNumberPeerCertificate());
      if (!options.expectedSerialNumbers().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedSerialNumbers(),
                  server_connection->ssl()->serialNumbersPeerCertificates());
        EXPECT_EQ(options.expectedSerialNumbers(),
                  server_connection->ssl()->serialNumbersPeerCertificates());
      }
      if (!options.expectedPeerIssuer().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedPeerIssuer(), server_connection->ssl()->issuerPeerCertificate());
        EXPECT_EQ(options.expectedPeerIssuer(), server_connection->ssl()->issuerPeerCertificate());
      }
      if (!options.expectedPeerSubject().empty()) {
        EXPECT_EQ(options.expectedPeerSubject(),
                  server_connection->ssl()->subjectPeerCertificate());
        EXPECT_EQ(options.expectedPeerSubject(),
                  server_connection->ssl()->subjectPeerCertificate());
      }
      if (!options.expectedLocalSubject().empty()) {
        EXPECT_EQ(options.expectedLocalSubject(),
                  server_connection->ssl()->subjectLocalCertificate());
        EXPECT_EQ(options.expectedLocalSubject(),
                  server_connection->ssl()->subjectLocalCertificate());
      }
      if (!options.expectedPeerOids().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_THAT(
            options.expectedPeerOids(),
            testing::UnorderedElementsAreArray(server_connection->ssl()->oidsPeerCertificate()));
        EXPECT_THAT(
            options.expectedPeerOids(),
            testing::UnorderedElementsAreArray(server_connection->ssl()->oidsPeerCertificate()));
      }
      if (!options.expectedLocalOids().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_THAT(
            options.expectedLocalOids(),
            testing::UnorderedElementsAreArray(server_connection->ssl()->oidsLocalCertificate()));
        EXPECT_THAT(
            options.expectedLocalOids(),
            testing::UnorderedElementsAreArray(server_connection->ssl()->oidsLocalCertificate()));
      }
      if (!options.expectedPeerCert().empty()) {
        std::string urlencoded = absl::StrReplaceAll(
            options.expectedPeerCert(),
            {{"\r", ""}, {"\n", "%0A"}, {" ", "%20"}, {"+", "%2B"}, {"/", "%2F"}, {"=", "%3D"}});
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(urlencoded, server_connection->ssl()->urlEncodedPemEncodedPeerCertificate());
        EXPECT_EQ(urlencoded, server_connection->ssl()->urlEncodedPemEncodedPeerCertificate());
      }
      if (!options.expectedPeerCertChain().empty()) {
        std::string cert_chain = absl::StrReplaceAll(
            options.expectedPeerCertChain(),
            {{"\r", ""}, {"\n", "%0A"}, {" ", "%20"}, {"+", "%2B"}, {"/", "%2F"}, {"=", "%3D"}});
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(cert_chain, server_connection->ssl()->urlEncodedPemEncodedPeerCertificateChain());
        EXPECT_EQ(cert_chain, server_connection->ssl()->urlEncodedPemEncodedPeerCertificateChain());
      }
      if (!options.expectedValidFromTimePeerCert().empty()) {
        const std::string formatted = TestUtility::formatTime(
            server_connection->ssl()->validFromPeerCertificate().value(), "%b %e %H:%M:%S %Y GMT");
        EXPECT_EQ(options.expectedValidFromTimePeerCert(), formatted);
      }
      if (!options.expectedExpirationTimePeerCert().empty()) {
        const std::string formatted = TestUtility::formatTime(
            server_connection->ssl()->expirationPeerCertificate().value(), "%b %e %H:%M:%S %Y GMT");
        EXPECT_EQ(options.expectedExpirationTimePeerCert(), formatted);
      }
      if (options.expectNoCert()) {
        EXPECT_FALSE(server_connection->ssl()->peerCertificatePresented());
        EXPECT_FALSE(server_connection->ssl()->validFromPeerCertificate().has_value());
        EXPECT_FALSE(server_connection->ssl()->expirationPeerCertificate().has_value());
        EXPECT_EQ(EMPTY_STRING, server_connection->ssl()->sha256PeerCertificateDigest());
        EXPECT_EQ(EMPTY_STRING, server_connection->ssl()->sha1PeerCertificateDigest());
        EXPECT_EQ(EMPTY_STRING, server_connection->ssl()->urlEncodedPemEncodedPeerCertificate());
        EXPECT_EQ(EMPTY_STRING, server_connection->ssl()->subjectPeerCertificate());
        EXPECT_EQ(std::vector<std::string>{}, server_connection->ssl()->dnsSansPeerCertificate());
        EXPECT_EQ(std::vector<std::string>{}, server_connection->ssl()->ipSansPeerCertificate());
        EXPECT_EQ(std::vector<std::string>{}, server_connection->ssl()->oidsPeerCertificate());
      }
      if (options.expectNoCertChain()) {
        EXPECT_EQ(EMPTY_STRING,
                  server_connection->ssl()->urlEncodedPemEncodedPeerCertificateChain());
      }
      if (!options.expectedSni().empty()) {
        EXPECT_EQ(options.expectedSni(), server_connection->ssl()->sni());
      }

      const SslHandshakerImpl* ssl_socket =
          dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
      SSL* client_ssl_socket = ssl_socket->ssl();
      const uint8_t* response_head;
      size_t response_len;
      SSL_get0_ocsp_response(client_ssl_socket, &response_head, &response_len);
      std::string ocsp_response{reinterpret_cast<const char*>(response_head), response_len};
      EXPECT_EQ(options.expectedOcspResponse(), ocsp_response);

      // By default, the session is not created with session resumption. The
      // client should see a session ID but the server should not.
      EXPECT_EQ(EMPTY_STRING, server_connection->ssl()->sessionId());
      EXPECT_NE(EMPTY_STRING, client_connection->ssl()->sessionId());

      server_connection->close(Network::ConnectionCloseType::NoFlush);
      client_connection->close(Network::ConnectionCloseType::NoFlush);
      dispatcher->exit();
    }
  };

  size_t close_count = 0;
  auto close_second_time = [&close_count, &dispatcher]() {
    if (++close_count == 2) {
      dispatcher->exit();
    }
  };

  if (options.expectSuccess()) {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  } else {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(options.expectedServerCloseEvent()))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));
  }

  if (options.expectedVerifyErrorCode() != -1) {
    EXPECT_LOG_CONTAINS("debug", X509_verify_cert_error_string(options.expectedVerifyErrorCode()),
                        dispatcher->run(Event::Dispatcher::RunType::Block));
  } else {
    dispatcher->run(Event::Dispatcher::RunType::Block);
  }

  if (!options.expectedServerStats().empty()) {
    EXPECT_EQ(1UL, server_stats_store.counter(options.expectedServerStats()).value());
  }

  if (!options.notExpectedClientStats().empty()) {
    EXPECT_EQ(0, client_stats_store.counter(options.notExpectedClientStats()).value());
  }

  if (options.expectSuccess()) {
    EXPECT_EQ("", client_connection->transportFailureReason());
    EXPECT_EQ("", server_connection->transportFailureReason());
  } else {
    EXPECT_THAT(std::string(client_connection->transportFailureReason()),
                ContainsRegex(options.expectedTransportFailureReasonContains()));
    EXPECT_NE("", server_connection->transportFailureReason());
  }
}

/**
 * A class to hold the options for testUtilV2().
 */
class TestUtilOptionsV2 : public TestUtilOptionsBase {
public:
  TestUtilOptionsV2(
      const envoy::config::listener::v3::Listener& listener,
      const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& client_ctx_proto,
      bool expect_success, Network::Address::IpVersion version,
      bool skip_server_failure_reason_check = false)
      : TestUtilOptionsBase(expect_success, version),
        skip_server_failure_reason_check_(skip_server_failure_reason_check), listener_(listener),
        client_ctx_proto_(client_ctx_proto), transport_socket_options_(nullptr) {
    if (expect_success) {
      setExpectedServerStats("ssl.handshake").setExpectedClientStats("ssl.handshake");
    } else {
      setExpectedServerStats("ssl.fail_verify_error")
          .setExpectedClientStats("ssl.connection_error");
    }
  }

  bool skipServerFailureReasonCheck() const { return skip_server_failure_reason_check_; }
  const envoy::config::listener::v3::Listener& listener() const { return listener_; }
  const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& clientCtxProto() const {
    return client_ctx_proto_;
  }
  const std::string& expectedClientStats() const { return expected_client_stats_; }

  TestUtilOptionsV2& setExpectedServerStats(const std::string& expected_server_stats) {
    TestUtilOptionsBase::setExpectedServerStats(expected_server_stats);
    return *this;
  }

  TestUtilOptionsV2& setExpectedClientCertUri(const std::string& expected_client_cert_uri) {
    TestUtilOptionsBase::setExpectedClientCertUri(expected_client_cert_uri);
    return *this;
  }

  TestUtilOptionsV2&
  setExpectedClientCertIpSans(const std::vector<std::string>& expected_client_cert_ips) {
    TestUtilOptionsBase::setExpectedClientIpSans(expected_client_cert_ips);
    return *this;
  }

  TestUtilOptionsV2& setExpectedClientStats(const std::string& expected_client_stats) {
    expected_client_stats_ = expected_client_stats;
    return *this;
  }

  TestUtilOptionsV2& setClientSession(const std::string& client_session) {
    client_session_ = client_session;
    return *this;
  }

  const std::string& clientSession() const { return client_session_; }

  TestUtilOptionsV2& setExpectedProtocolVersion(const std::string& expected_protocol_version) {
    expected_protocol_version_ = expected_protocol_version;
    return *this;
  }

  const std::string& expectedProtocolVersion() const { return expected_protocol_version_; }

  TestUtilOptionsV2& setExpectedCiphersuite(const std::string& expected_cipher_suite) {
    expected_cipher_suite_ = expected_cipher_suite;
    return *this;
  }

  const std::string& expectedCiphersuite() const { return expected_cipher_suite_; }

  TestUtilOptionsV2& setExpectedServerCertDigest(const std::string& expected_server_cert_digest) {
    expected_server_cert_digest_ = expected_server_cert_digest;
    return *this;
  }

  const std::string& expectedServerCertDigest() const { return expected_server_cert_digest_; }

  TestUtilOptionsV2&
  setExpectedRequestedServerName(const std::string& expected_requested_server_name) {
    expected_requested_server_name_ = expected_requested_server_name;
    return *this;
  }

  const std::string& expectedRequestedServerName() const { return expected_requested_server_name_; }

  TestUtilOptionsV2& setExpectedALPNProtocol(const std::string& expected_alpn_protocol) {
    expected_alpn_protocol_ = expected_alpn_protocol;
    return *this;
  }

  const std::string& expectedALPNProtocol() const { return expected_alpn_protocol_; }

  TestUtilOptionsV2& setTransportSocketOptions(
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options) {
    transport_socket_options_ = transport_socket_options;
    return *this;
  }

  Network::TransportSocketOptionsConstSharedPtr transportSocketOptions() const {
    return transport_socket_options_;
  }

  TestUtilOptionsV2& setExpectedTransportFailureReasonContains(
      const std::string& expected_transport_failure_reason_contains) {
    expected_transport_failure_reason_contains_ = expected_transport_failure_reason_contains;
    return *this;
  }

  const std::string& expectedTransportFailureReasonContains() const {
    return expected_transport_failure_reason_contains_;
  }

private:
  bool skip_server_failure_reason_check_;
  const envoy::config::listener::v3::Listener& listener_;
  const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& client_ctx_proto_;
  std::string expected_client_stats_;

  std::string client_session_;
  std::string expected_cipher_suite_;
  std::string expected_protocol_version_;
  std::string expected_server_cert_digest_;
  std::string expected_requested_server_name_;
  std::string expected_alpn_protocol_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  std::string expected_transport_failure_reason_contains_;
};

void testUtilV2(const TestUtilOptionsV2& options) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      transport_socket_factory_context;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);

  // SNI-based selection logic isn't happening in SslSocket anymore.
  ASSERT(options.listener().filter_chains().size() == 1);
  const auto& filter_chain = options.listener().filter_chains(0);
  std::vector<std::string> server_names(filter_chain.filter_chain_match().server_names().begin(),
                                        filter_chain.filter_chain_match().server_names().end());
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system);
  NiceMock<Runtime::MockLoader> runtime;
  ON_CALL(transport_socket_factory_context.server_context_, api())
      .WillByDefault(ReturnRef(*server_api));

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const envoy::config::core::v3::TransportSocket& transport_socket =
      filter_chain.transport_socket();
  ASSERT(transport_socket.has_typed_config());
  transport_socket.typed_config().UnpackTo(&tls_context);

  auto server_cfg =
      *ServerContextConfigImpl::create(tls_context, transport_socket_factory_context, false);

  auto factory_or_error = ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), server_names);
  THROW_IF_NOT_OK(factory_or_error.status());
  auto server_ssl_socket_factory = std::move(*factory_or_error);

  Event::DispatcherPtr dispatcher(server_api->allocateDispatcher("test_thread"));
  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(options.version()));
  NiceMock<Network::MockTcpListenerCallbacks> callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime, listener_config, overload_state,
                     server_api->randomGenerator(), *dispatcher);

  Stats::TestUtil::TestStore client_stats_store;
  Api::ApiPtr client_api = Api::createApiForTest(client_stats_store, time_system);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      client_factory_context;
  ON_CALL(client_factory_context.server_context_, api()).WillByDefault(ReturnRef(*client_api));

  auto client_cfg =
      *ClientContextConfigImpl::create(options.clientCtxProto(), client_factory_context);
  auto client_factory_or_error = ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                                *client_stats_store.rootScope());
  THROW_IF_NOT_OK(client_factory_or_error.status());
  auto client_ssl_socket_factory = std::move(*client_factory_or_error);
  Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(options.transportSocketOptions(), nullptr),
      nullptr, nullptr);

  if (!options.clientSession().empty()) {
    const SslHandshakerImpl* ssl_socket =
        dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
    SSL* client_ssl_socket = ssl_socket->ssl();
    SSL_CTX* client_ssl_context = SSL_get_SSL_CTX(client_ssl_socket);
    SSL_SESSION* client_ssl_session =
        SSL_SESSION_from_bytes(reinterpret_cast<const uint8_t*>(options.clientSession().data()),
                               options.clientSession().size(), client_ssl_context);
    int rc = SSL_set_session(client_ssl_socket, client_ssl_session);
    ASSERT(rc == 1);
    SSL_SESSION_free(client_ssl_session);
  }

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        std::string sni = options.transportSocketOptions() != nullptr &&
                                  options.transportSocketOptions()->serverNameOverride().has_value()
                              ? options.transportSocketOptions()->serverNameOverride().value()
                              : options.clientCtxProto().sni();
        socket->setRequestedServerName(sni);
        Network::TransportSocketPtr transport_socket =
            server_ssl_socket_factory->createDownstreamTransportSocket();
        EXPECT_FALSE(transport_socket->startSecureTransport());
        server_connection = dispatcher->createServerConnection(
            std::move(socket), std::move(transport_socket), stream_info);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  size_t connect_count = 0;
  auto connect_second_time = [&]() {
    if (++connect_count == 2) {
      if (!options.expectedServerCertDigest().empty()) {
        EXPECT_EQ(options.expectedServerCertDigest(),
                  client_connection->ssl()->sha256PeerCertificateDigest());
      }
      if (!options.expectedALPNProtocol().empty()) {
        EXPECT_EQ(options.expectedALPNProtocol(), client_connection->nextProtocol());
      }
      // Assert twice to ensure a cached value is returned and still valid.
      EXPECT_EQ(options.expectedClientCertUri(), server_connection->ssl()->uriSanPeerCertificate());
      EXPECT_EQ(options.expectedClientCertUri(), server_connection->ssl()->uriSanPeerCertificate());
      EXPECT_EQ(options.expectedClientCertIpSans(),
                server_connection->ssl()->ipSansPeerCertificate());
      // Assert twice to ensure a cached value is returned and still valid.
      EXPECT_EQ(options.expectedClientCertIpSans(),
                server_connection->ssl()->ipSansPeerCertificate());
      const SslHandshakerImpl* ssl_socket =
          dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
      SSL* client_ssl_socket = ssl_socket->ssl();
      if (!options.expectedProtocolVersion().empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(options.expectedProtocolVersion(), client_connection->ssl()->tlsVersion());
        EXPECT_EQ(options.expectedProtocolVersion(), client_connection->ssl()->tlsVersion());
      }
      if (!options.expectedCiphersuite().empty()) {
        EXPECT_EQ(options.expectedCiphersuite(), client_connection->ssl()->ciphersuiteString());
        const SSL_CIPHER* cipher =
            SSL_get_cipher_by_value(client_connection->ssl()->ciphersuiteId());
        EXPECT_NE(nullptr, cipher);
        EXPECT_EQ(options.expectedCiphersuite(), SSL_CIPHER_get_name(cipher));
      }

      absl::optional<std::string> server_ssl_requested_server_name;
      const SslHandshakerImpl* server_ssl_socket =
          dynamic_cast<const SslHandshakerImpl*>(server_connection->ssl().get());
      SSL* server_ssl = server_ssl_socket->ssl();
      auto requested_server_name = SSL_get_servername(server_ssl, TLSEXT_NAMETYPE_host_name);
      if (requested_server_name != nullptr) {
        server_ssl_requested_server_name = std::string(requested_server_name);
      }

      if (!options.expectedRequestedServerName().empty()) {
        EXPECT_TRUE(server_ssl_requested_server_name.has_value());
        EXPECT_EQ(options.expectedRequestedServerName(), server_ssl_requested_server_name.value());
      } else {
        EXPECT_FALSE(server_ssl_requested_server_name.has_value());
      }

      const uint16_t tls_version = SSL_version(client_ssl_socket);
      if (SSL3_VERSION <= tls_version && tls_version <= TLS1_2_VERSION) {
        // Prior to TLS 1.3, one should be able to resume the session. With TLS
        // 1.3, tickets come after the handshake and the SSL_SESSION on the
        // client is a dummy object.
        SSL_SESSION* client_ssl_session = SSL_get_session(client_ssl_socket);
        EXPECT_TRUE(SSL_SESSION_is_resumable(client_ssl_session));
      }
      server_connection->close(Network::ConnectionCloseType::NoFlush);
      client_connection->close(Network::ConnectionCloseType::NoFlush);
      dispatcher->exit();
    }
  };

  size_t close_count = 0;
  auto close_second_time = [&close_count, &dispatcher]() {
    if (++close_count == 2) {
      dispatcher->exit();
    }
  };

  if (options.expectSuccess()) {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          EXPECT_EQ(options.expectedRequestedServerName(),
                    server_connection->requestedServerName());
          connect_second_time();
        }));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  } else {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));
  }

  dispatcher->run(Event::Dispatcher::RunType::Block);

  if (!options.expectedServerStats().empty()) {
    EXPECT_EQ(1UL, server_stats_store.counter(options.expectedServerStats()).value())
        << options.expectedServerStats();
  }

  if (!options.expectedClientStats().empty()) {
    EXPECT_EQ(1UL, client_stats_store.counter(options.expectedClientStats()).value());
  }

  if (options.expectSuccess()) {
    EXPECT_EQ("", client_connection->transportFailureReason());
    EXPECT_EQ("", server_connection->transportFailureReason());
  } else {
    EXPECT_THAT(std::string(client_connection->transportFailureReason()),
                ContainsRegex(options.expectedTransportFailureReasonContains()));
    if (!options.skipServerFailureReasonCheck()) {
      EXPECT_NE("", server_connection->transportFailureReason());
    }
  }
}

void updateFilterChain(
    const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& tls_context,
    envoy::config::listener::v3::FilterChain& filter_chain) {
  filter_chain.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
}

struct OptionalServerConfig {
  absl::optional<std::string> cert_hash{};
  absl::optional<std::string> trusted_ca{};
  absl::optional<bool> allow_expired_cert{};
};

void configureServerAndExpiredClientCertificate(
    envoy::config::listener::v3::Listener& listener,
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& client,
    const OptionalServerConfig& server_config) {
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"));

  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  if (server_config.trusted_ca.has_value()) {
    server_validation_ctx->mutable_trusted_ca()->set_filename(
        TestEnvironment::substitute(server_config.trusted_ca.value()));
  } else {
    server_validation_ctx->mutable_trusted_ca()->set_filename(
        TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  }
  if (server_config.allow_expired_cert.has_value()) {
    server_validation_ctx->set_allow_expired_certificate(server_config.allow_expired_cert.value());
  }
  if (server_config.cert_hash.has_value()) {
    server_validation_ctx->add_verify_certificate_hash(server_config.cert_hash.value());
  }
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/expired_san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/expired_san_uri_key.pem"));
}

TestUtilOptionsV2 createProtocolTestOptions(
    const envoy::config::listener::v3::Listener& listener,
    const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& client_ctx,
    Network::Address::IpVersion version, std::string protocol) {
  std::string stats = "ssl.versions." + protocol;
  TestUtilOptionsV2 options(listener, client_ctx, true, version);
  options.setExpectedServerStats(stats).setExpectedClientStats(stats);
  return options.setExpectedProtocolVersion(protocol);
}

} // namespace

class SslSocketTest : public SslCertsTest,
                      public testing::WithParamInterface<Network::Address::IpVersion> {
protected:
  SslSocketTest()
      : dispatcher_(api_->allocateDispatcher("test_thread")),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::Connection),
        version_(GetParam()) {}

  void testClientSessionResumption(const std::string& server_ctx_yaml,
                                   const std::string& client_ctx_yaml, bool expect_reuse,
                                   const Network::Address::IpVersion version);

  Network::ListenerPtr createListener(Network::SocketSharedPtr&& socket,
                                      Network::TcpListenerCallbacks& cb, Runtime::Loader& runtime,
                                      const Network::ListenerConfig& listener_config,
                                      Server::ThreadLocalOverloadStateOptRef overload_state,
                                      Event::Dispatcher& dispatcher) {
    return std::make_unique<Network::TcpListenerImpl>(
        dispatcher, api_->randomGenerator(), runtime, std::move(socket), cb,
        listener_config.bindToPort(), listener_config.ignoreGlobalConnLimit(),
        listener_config.shouldBypassOverloadManager(),
        listener_config.maxConnectionsToAcceptPerSocketEvent(), overload_state);
  }

  NiceMock<Runtime::MockLoader> runtime_;
  Event::DispatcherPtr dispatcher_;
  StreamInfo::StreamInfoImpl stream_info_;
  Network::Address::IpVersion version_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslSocketTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SslSocketTest, ServerTransportSocketOptions) {
  Stats::TestUtil::TestStore server_stats_store;
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";
  ;
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, false);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  auto server_ssl_socket_factory =
      ServerSslSocketFactory::create(std::move(server_cfg), manager,
                                     *server_stats_store.rootScope(), std::vector<std::string>{})
          .value();
  auto ssl_socket = server_ssl_socket_factory->createDownstreamTransportSocket();
  auto ssl_handshaker = dynamic_cast<const SslHandshakerImpl*>(ssl_socket->ssl().get());
  auto shared_ptr_ptr = static_cast<const Network::TransportSocketOptionsConstSharedPtr*>(
      SSL_get_app_data(ssl_handshaker->ssl()));
  ASSERT_NE(nullptr, shared_ptr_ptr);
  EXPECT_EQ(nullptr, (*shared_ptr_ptr).get());
}

TEST_P(SslSocketTest, GetCertDigest) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSha256Digest(TEST_NO_SAN_CERT_256_HASH)
               .setExpectedSha1Digest(TEST_NO_SAN_CERT_1_HASH)
               .setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL));
}

TEST_P(SslSocketTest, GetCertDigestInvalidFiles) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(
      test_options.setExpectedSha256Digest("").setExpectedSha1Digest("").setExpectedSerialNumber(
          ""));
}

TEST_P(SslSocketTest, GetCertDigests) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  std::vector<std::string> sha256Digests = {TEST_NO_SAN_CERT_256_HASH,
                                            TEST_INTERMEDIATE_CA_CERT_256_HASH};
  std::vector<std::string> sha1Digests = {TEST_NO_SAN_CERT_1_HASH,
                                          TEST_INTERMEDIATE_CA_CERT_1_HASH};
  std::vector<std::string> serialNumbers = {TEST_NO_SAN_CERT_SERIAL,
                                            TEST_INTERMEDIATE_CA_CERT_SERIAL};
  testUtil(test_options.setExpectedSha256Digests(sha256Digests)
               .setExpectedSha1Digests(sha1Digests)
               .setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL) // test checks first serial #
               .setExpectedSerialNumbers(serialNumbers));
}

TEST_P(SslSocketTest, GetCertDigestsInvalidFiles) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  std::vector<std::string> emptyStringVec;
  testUtil(test_options.setExpectedSha256Digests(emptyStringVec)
               .setExpectedSha1Digests(emptyStringVec)
               .setExpectedSerialNumbers(emptyStringVec));
}

TEST_P(SslSocketTest, GetCertDigestInline) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  // From test/common/tls/test_data/san_dns_cert.pem.
  server_cert->mutable_certificate_chain()->set_inline_bytes(
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
          "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem")));

  // From test/common/tls/test_data/san_dns_key.pem.
  server_cert->mutable_private_key()->set_inline_bytes(TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem")));

  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  // From test/common/tls/test_data/ca_certificates.pem.
  server_validation_ctx->mutable_trusted_ca()->set_inline_bytes(
      TestEnvironment::readFileToStringForTest(
          TestEnvironment::substitute("{{ test_rundir "
                                      "}}/test/common/tls/test_data/ca_certificates.pem")));

  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_ctx;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client_ctx.mutable_common_tls_context()->add_tls_certificates();

  // From test/common/tls/test_data/san_uri_cert.pem.
  client_cert->mutable_certificate_chain()->set_inline_bytes(
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
          "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem")));

  // From test/common/tls/test_data/san_uri_key.pem.
  client_cert->mutable_private_key()->set_inline_bytes(TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem")));

  TestUtilOptionsV2 test_options(listener, client_ctx, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));
}

TEST_P(SslSocketTest, GetCertDigestInlineWithIpSanClientCerts) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  // From test/common/tls/test_data/san_dns_cert.pem.
  server_cert->mutable_certificate_chain()->set_inline_bytes(
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
          "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem")));

  // From test/common/tls/test_data/san_dns_key.pem.
  server_cert->mutable_private_key()->set_inline_bytes(TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem")));

  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  // From test/common/tls/test_data/ca_certificates.pem.
  server_validation_ctx->mutable_trusted_ca()->set_inline_bytes(
      TestEnvironment::readFileToStringForTest(
          TestEnvironment::substitute("{{ test_rundir "
                                      "}}/test/common/tls/test_data/ca_certificates.pem")));

  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_ctx;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client_ctx.mutable_common_tls_context()->add_tls_certificates();

  // From test/common/tls/test_data/san_ip_cert.pem.
  client_cert->mutable_certificate_chain()->set_inline_bytes(
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
          "{{ test_rundir }}/test/common/tls/test_data/san_ip_cert.pem")));

  // From test/common/tls/test_data/san_ip_key.pem.
  client_cert->mutable_private_key()->set_inline_bytes(TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_ip_key.pem")));

  TestUtilOptionsV2 test_options(listener, client_ctx, true, version_);
  testUtilV2(test_options.setExpectedClientCertIpSans({"1.1.1.1"})
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));
}

TEST_P(SslSocketTest, GetCertDigestServerCertWithIntermediateCA) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSha256Digest(TEST_NO_SAN_CERT_256_HASH)
               .setExpectedSha1Digest(TEST_NO_SAN_CERT_1_HASH)
               .setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL));
}

TEST_P(SslSocketTest, GetCertDigestServerCertWithoutCommonName) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_only_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_only_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSha256Digest(TEST_NO_SAN_CERT_256_HASH)
               .setExpectedSha1Digest(TEST_NO_SAN_CERT_1_HASH)
               .setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL));
}

TEST_P(SslSocketTest, GetUriWithUriSan) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      match_typed_subject_alt_names:
      - san_type: URI
        matcher:
          exact: "spiffe://lyft.com/test-team"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
               .setExpectedSerialNumber(TEST_SAN_URI_CERT_SERIAL));
}

// Verify that IP SANs work with an IPv4 address specified in the validation context.
TEST_P(SslSocketTest, Ipv4San) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamcacert.pem"
      match_typed_subject_alt_names:
      - san_type: IP_ADDRESS
        matcher:
          exact: "127.0.0.1"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamlocalhostcert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamlocalhostkey.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options);
}

// Verify that IP SANs work with an IPv6 address specified in the validation context.
TEST_P(SslSocketTest, Ipv6San) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamcacert.pem"
      match_typed_subject_alt_names:
      - san_type: IP_ADDRESS
        matcher:
          exact: "::1"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamlocalhostcert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/config/integration/certs/upstreamlocalhostkey.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options);
}

TEST_P(SslSocketTest, GetNoUriWithDnsSan) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  // The SAN field only has DNS, expect "" for uriSanPeerCertificate().
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSerialNumber(TEST_SAN_DNS_CERT_SERIAL));
}

TEST_P(SslSocketTest, NoCert) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedServerStats("ssl.no_certificate")
               .setExpectNoCert()
               .setExpectNoCertChain());
}

TEST_P(SslSocketTest, NoLocalCert) {
  ContextManagerImpl manager(factory_context_.serverFactoryContext());
  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::TestUtil::TestStore client_stats_store;
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                                   *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);

  EXPECT_EQ(std::vector<std::string>{}, client_connection->ssl()->uriSanLocalCertificate());
  EXPECT_EQ(std::vector<std::string>{}, client_connection->ssl()->dnsSansLocalCertificate());
  EXPECT_EQ(std::vector<std::string>{}, client_connection->ssl()->ipSansLocalCertificate());
  EXPECT_EQ(std::vector<std::string>{}, client_connection->ssl()->oidsLocalCertificate());
  EXPECT_EQ(EMPTY_STRING, client_connection->ssl()->subjectLocalCertificate());

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  client_connection->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

// Prefer ECDSA certificate when multiple RSA certificates are present and the
// client is RSA/ECDSA capable. We validate TLSv1.2 only here, since we validate
// the e2e behavior on TLSv1.2/1.3 in ssl_integration_test.
TEST_P(SslSocketTest, MultiCertPreferEcdsaWithoutSni) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SELFSIGNED_ECDSA_P256_CERT_256_HASH);

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options);
}

// When client supports SNI, exact match is preferred over wildcard match.
TEST_P(SslSocketTest, MultiCertPreferExactSniMatch) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "server1.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SAN_DNS_RSA_1_CERT_256_HASH);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSni("server1.example.com"));
}

// When client supports SNI and multiple certs have a matching SAN, prefer the earlier
// cert in the list.
TEST_P(SslSocketTest, MultiCertPreferFirstCertWithSAN) {
  {
    const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "server1.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                     TEST_SAN_DNS_RSA_1_CERT_256_HASH);
    const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_key.pem"
)EOF";

    TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
    testUtil(test_options.setExpectedSni("server1.example.com"));
  }

  // Now do the same test but with `tls_certificates` in the opposite order, and the client
  // validating the other certificate.
  {
    const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "server1.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                     TEST_SAN_MULTIPLE_DNS_1_CERT_256_HASH);
    const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
)EOF";

    TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
    testUtil(test_options.setExpectedSni("server1.example.com"));
  }
}

// When client supports SNI and there is no exact match, validate that wildcard "*.example.com"
// matches to "wildcardonlymatch.example.com".
TEST_P(SslSocketTest, MultiCertWildcardSniMatch) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "wildcardonlymatch.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SAN_MULTIPLE_DNS_CERT_256_HASH);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSni("wildcardonlymatch.example.com"));
}

// When client supports SNI and there is no exact match, validate that wildcard SAN *.example.com
// does not matches to a.wildcardonlymatch.example.com, so that the default/first cert is used.
TEST_P(SslSocketTest, MultiCertWildcardSniMismatch) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "a.wildcardonlymatch.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_NO_SAN_CERT_256_HASH);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  // The validation succeeds with default cert that does not match to SNI since Envoy does not
  // define the criteria that how to validate cert SAN based on SNI .
  testUtil(test_options.setExpectedSni("a.wildcardonlymatch.example.com"));
}

// On SNI match, the ECDSA certificate is preferred over RSA.
TEST_P(SslSocketTest, MultiCertPreferEcdsaOnSniMatch) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "server1.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SAN_DNS_ECDSA_1_CERT_256_HASH);

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_2_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_2_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSni("server1.example.com"));
}

// On SNI match, the RSA certificate will be selected if ECDSA cert is not present.
TEST_P(SslSocketTest, MultiCertPickRSAOnSniMatch) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "server1.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SAN_DNS_RSA_1_CERT_256_HASH);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_2_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_2_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSni("server1.example.com"));
}

// On SNI mismatch, if full scan is disabled, validate that the first cert is used.
TEST_P(SslSocketTest, MultiCertWithFullScanDisabledOnSniMismatch) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "nomatch.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_NO_SAN_CERT_256_HASH);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  // The validation succeeds with the default cert that does not match to SNI, because Envoy does
  // not define the criteria that how to validate cert SAN based on SNI .
  testUtil(test_options.setExpectedSni("nomatch.example.com"));
}

// On SNI mismatch, full scan will be executed if it is enabled, validate that ECDSA cert is
// preferred over RSA cert.
TEST_P(SslSocketTest, MultiCertPreferEcdsaWithFullScanEnabledOnSniMismatch) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "nomatch.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SAN_DNS_ECDSA_1_CERT_256_HASH);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_key.pem"
  full_scan_certs_on_sni_mismatch: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  // The validation succeeds with the certificate that does not match to SNI, because Envoy does not
  // define the criteria that how to validate cert SAN based on SNI .
  testUtil(test_options.setExpectedSni("nomatch.example.com"));
}

// EC cert is selected for a no-EC-capable client.
TEST_P(SslSocketTest, CertWithNotECCapable) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    sni: "server1.example.com"
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SAN_DNS_ECDSA_1_CERT_256_HASH);
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  // TODO(luyao): We might need to modify ssl socket to set proper stats for failed handshake
  testUtil(test_options.setExpectedServerStats("")
               .setExpectedSni("server1.example.com")
               .setExpectedTransportFailureReasonContains("HANDSHAKE_FAILURE_ON_CLIENT_HELLO"));
}

TEST_P(SslSocketTest, GetUriWithLocalUriSan) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedLocalUri("spiffe://lyft.com/test-team")
               .setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL));
}

TEST_P(SslSocketTest, GetDnsWithLocalDnsSan) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedServerStats("ssl.no_certificate")
               .setExpectNoCert()
               .setExpectNoCertChain()
               .setExpectedLocalDns("server1.example.com"));
}

TEST_P(SslSocketTest, GetIpWithLocalIpSan) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_ip_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_ip_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedServerStats("ssl.no_certificate")
               .setExpectNoCert()
               .setExpectNoCertChain()
               .setExpectedLocalIp("1.1.1.1"));
}

TEST_P(SslSocketTest, GetSubjectsWithBothCerts) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL)
               .setExpectedPeerIssuer(
                   "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedPeerSubject(
                   "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedLocalSubject(
                   "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US"));
}

TEST_P(SslSocketTest, GetOidsWithBothCerts) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/extensions_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSerialNumber(TEST_EXTENSIONS_CERT_SERIAL)
               .setExpectedPeerOids({"2.5.29.14", "2.5.29.15", "2.5.29.19", "2.5.29.35",
                                     "2.5.29.37", "1.2.3.4.5.6.7.8", "1.2.3.4.5.6.7.9"})
               .setExpectedLocalOids({"2.5.29.14"}));
}

TEST_P(SslSocketTest, GetOidsWithLocalNoExtensionCert) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_extension_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_extension_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedServerStats("ssl.no_certificate")
               .setExpectNoCert()
               .setExpectNoCertChain()
               .setExpectedLocalOids({}));
}

TEST_P(SslSocketTest, GetPeerCert) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  std::string expected_peer_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"));
  testUtil(test_options.setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL)
               .setExpectedPeerIssuer(
                   "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedPeerSubject(
                   "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedLocalSubject(
                   "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedPeerCert(expected_peer_cert));
}

TEST_P(SslSocketTest, GetPeerCertAcceptUntrusted) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"
      trust_chain_verification: ACCEPT_UNTRUSTED
  require_client_certificate: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  std::string expected_peer_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"));
  testUtil(test_options.setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL)
               .setExpectedPeerIssuer(
                   "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedPeerSubject(
                   "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedLocalSubject(
                   "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US")
               .setExpectedPeerCert(expected_peer_cert));
}

TEST_P(SslSocketTest, NoCertUntrustedNotPermitted) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"
      trust_chain_verification: VERIFY_TRUST_CHAIN
      verify_certificate_hash: "0000000000000000000000000000000000000000000000000000000000000000"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_no_cert"));
}

TEST_P(SslSocketTest, NoCertUntrustedPermitted) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"
      trust_chain_verification: ACCEPT_UNTRUSTED
      verify_certificate_hash: "0000000000000000000000000000000000000000000000000000000000000000"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedServerStats("ssl.no_certificate")
               .setExpectNoCert()
               .setExpectNoCertChain());
}

TEST_P(SslSocketTest, GetPeerCertChain) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  std::string expected_peer_cert_chain = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/no_san_chain.pem"));
  testUtil(test_options.setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL)
               .setExpectedPeerCertChain(expected_peer_cert_chain));
}

TEST_P(SslSocketTest, GetIssueExpireTimesPeerCert) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL)
               .setExpectedValidFromTimePeerCert(TEST_NO_SAN_CERT_NOT_BEFORE)
               .setExpectedExpirationTimePeerCert(TEST_NO_SAN_CERT_NOT_AFTER));
}

TEST_P(SslSocketTest, FailedClientAuthCaVerificationNoClientCert) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_no_cert"));
}

TEST_P(SslSocketTest, FailedClientAuthCaVerification) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY));
}

TEST_P(SslSocketTest, FailedClientAuthSanVerificationNoClientCert) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      match_typed_subject_alt_names:
      - san_type: DNS
        matcher:
          exact: "example.com"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_no_cert"));
}

TEST_P(SslSocketTest, FailedClientAuthSanVerification) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      match_typed_subject_alt_names:
      - san_type: DNS
        matcher:
          exact: "example.com"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_san"));
}

TEST_P(SslSocketTest, X509ExtensionsCertificateSerialNumber) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/extensions_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/extensions_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSerialNumber(TEST_EXTENSIONS_CERT_SERIAL));
}

// By default, expired certificates are not permitted.
TEST_P(SslSocketTest, FailedClientCertificateDefaultExpirationVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  configureServerAndExpiredClientCertificate(listener, client, /*server_config=*/{});

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedTransportFailureReasonContains("SSLV3_ALERT_CERTIFICATE_EXPIRED"));
}

// Expired certificates will not be accepted when explicitly disallowed via
// allow_expired_certificate.
TEST_P(SslSocketTest, FailedClientCertificateExpirationVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  OptionalServerConfig server_config;
  server_config.allow_expired_cert = false;
  configureServerAndExpiredClientCertificate(listener, client, server_config);

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedTransportFailureReasonContains("SSLV3_ALERT_CERTIFICATE_EXPIRED"));
}

// Expired certificates will be accepted when explicitly allowed via allow_expired_certificate.
TEST_P(SslSocketTest, ClientCertificateExpirationAllowedVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  OptionalServerConfig server_config;
  server_config.allow_expired_cert = true;
  configureServerAndExpiredClientCertificate(listener, client, server_config);

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedTransportFailureReasonContains("SSLV3_ALERT_CERTIFICATE_EXPIRED"));
}

// Allow expired certificates, but add a certificate hash requirement so it still fails.
TEST_P(SslSocketTest, FailedClientCertAllowExpiredBadHashVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  OptionalServerConfig server_config;
  server_config.allow_expired_cert = true;
  server_config.cert_hash = "0000000000000000000000000000000000000000000000000000000000000000";
  configureServerAndExpiredClientCertificate(listener, client, server_config);

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_cert_hash")
                 .setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedTransportFailureReasonContains("TLSV1.*_BAD_CERTIFICATE_HASH_VALUE"));
}

// Allow expired certificates, but use the wrong CA so it should fail still.
TEST_P(SslSocketTest, FailedClientCertAllowServerExpiredWrongCAVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  OptionalServerConfig server_config;
  server_config.allow_expired_cert = true;
  // Fake CA is not used to sign the client's certificate.
  server_config.trusted_ca = "{{ test_rundir "
                             "}}/test/common/tls/test_data/fake_ca_cert.pem";
  configureServerAndExpiredClientCertificate(listener, client, server_config);

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedTransportFailureReasonContains("TLSV1_ALERT_UNKNOWN_CA"));
}

TEST_P(SslSocketTest, ClientCertificateHashVerification) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = absl::StrCat(R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      verify_certificate_hash: ")EOF",
                                                   TEST_SAN_URI_CERT_256_HASH, "\"");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
               .setExpectedSerialNumber(TEST_SAN_URI_CERT_SERIAL));
}

TEST_P(SslSocketTest, ClientCertificateHashVerificationNoCA) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = absl::StrCat(R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      verify_certificate_hash: ")EOF",
                                                   TEST_SAN_URI_CERT_256_HASH, "\"");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
               .setExpectedSerialNumber(TEST_SAN_URI_CERT_SERIAL));
}

TEST_P(SslSocketTest, ClientCertificateHashListVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_hash(TEST_SAN_URI_CERT_256_HASH);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, ClientCertificateHashListVerificationNoCA) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_hash(TEST_SAN_URI_CERT_256_HASH);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationNoClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = absl::StrCat(R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      verify_certificate_hash: ")EOF",
                                                   TEST_SAN_URI_CERT_256_HASH, "\"");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_no_cert"));
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationNoCANoClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = absl::StrCat(R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      verify_certificate_hash: ")EOF",
                                                   TEST_SAN_URI_CERT_256_HASH, "\"");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_no_cert"));
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationWrongClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = absl::StrCat(R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      verify_certificate_hash: ")EOF",
                                                   TEST_SAN_URI_CERT_256_HASH, "\"");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_cert_hash"));
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationNoCAWrongClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = absl::StrCat(R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      verify_certificate_hash: ")EOF",
                                                   TEST_SAN_URI_CERT_256_HASH, "\"");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_cert_hash"));
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationWrongCA) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = absl::StrCat(R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"
      verify_certificate_hash: ")EOF",
                                                   TEST_SAN_URI_CERT_256_HASH, "\"");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY));
}

TEST_P(SslSocketTest, CertificatesWithPassword) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_key.pem"));
  server_cert->mutable_password()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_password.txt"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_hash(TEST_PASSWORD_PROTECTED_CERT_256_HASH);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_key.pem"));
  client_cert->mutable_password()->set_inline_string(TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_password.txt")));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_PASSWORD_PROTECTED_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, Pkcs12CertificatesWithPassword) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  server_cert->mutable_pkcs12()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_certkey.p12"));
  server_cert->mutable_password()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_password.txt"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_hash(TEST_PASSWORD_PROTECTED_CERT_256_HASH);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_pkcs12()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_certkey.p12"));
  client_cert->mutable_password()->set_inline_string(TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_password.txt")));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_PASSWORD_PROTECTED_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, Pkcs12CertificatesWithoutPassword) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  server_cert->mutable_pkcs12()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/san_dns3_certkeychain.p12"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_hash(TEST_SAN_DNS3_CERT_256_HASH);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_pkcs12()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/san_dns3_certkeychain.p12"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedServerCertDigest(TEST_SAN_DNS3_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, ClientCertificateSpkiVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();

  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, ClientCertificateSpkiVerificationNoCA) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationNoClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_no_cert")
                 .setExpectedTransportFailureReasonContains("SSLV3_ALERT_HANDSHAKE_FAILURE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationNoCANoClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_no_cert")
                 .setExpectedTransportFailureReasonContains("SSLV3_ALERT_HANDSHAKE_FAILURE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationWrongClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_cert_hash")
                 .setExpectedTransportFailureReasonContains("TLSV1.*_BAD_CERTIFICATE_HASH_VALUE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationNoCAWrongClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_cert_hash")
                 .setExpectedTransportFailureReasonContains("TLSV1.*_BAD_CERTIFICATE_HASH_VALUE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationWrongCA) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedTransportFailureReasonContains("TLSV1_ALERT_UNKNOWN_CA"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, ClientCertificateHashAndSpkiVerification) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, ClientCertificateHashAndSpkiVerificationNoCA) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_DNS_CERT_SPKI);
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
                 .setExpectedServerCertDigest(TEST_SAN_DNS_CERT_256_HASH));

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationNoClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_no_cert")
                 .setExpectedTransportFailureReasonContains("SSLV3_ALERT_HANDSHAKE_FAILURE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationNoCANoClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_no_cert")
                 .setExpectedTransportFailureReasonContains("SSLV3_ALERT_HANDSHAKE_FAILURE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationWrongClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_cert_hash")
                 .setExpectedTransportFailureReasonContains("TLSV1.*_BAD_CERTIFICATE_HASH_VALUE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationNoCAWrongClientCertificate) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedServerStats("ssl.fail_verify_cert_hash")
                 .setExpectedTransportFailureReasonContains("TLSV1.*_BAD_CERTIFICATE_HASH_VALUE"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationWrongCA) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(TEST_SAN_URI_CERT_SPKI);
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  TestUtilOptionsV2 test_options(listener, client, false, version_);
  testUtilV2(test_options.setExpectedTransportFailureReasonContains("TLSV1_ALERT_UNKNOWN_CA"));

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
}

// Make sure that we do not flush code and do an immediate close if we have not completed the
// handshake.
TEST_P(SslSocketTest, FlushCloseDuringHandshake) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_certificates.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, false);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime_, listener_config, overload_state, *dispatcher_);

  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr, nullptr);
  client_connection->connect();
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
        Buffer::OwnedImpl data("hello");
        server_connection->write(data, false);
        server_connection->close(Network::ConnectionCloseType::FlushWrite);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that half-close is sent and received correctly
TEST_P(SslSocketTest, HalfClose) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_certificates.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(server_tls_context, factory_context_, false);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks listener_callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener = createListener(socket, listener_callbacks, runtime_,
                                                 listener_config, overload_state, *dispatcher_);
  std::shared_ptr<Network::MockReadFilter> server_read_filter(new Network::MockReadFilter());
  std::shared_ptr<Network::MockReadFilter> client_read_filter(new Network::MockReadFilter());

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::TestUtil::TestStore client_stats_store;
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                                   *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  client_connection->enableHalfClose(true);
  client_connection->addReadFilter(client_read_filter);
  client_connection->connect();
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(listener_callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->enableHalfClose(true);
        server_connection->addReadFilter(server_read_filter);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
        Buffer::OwnedImpl data("hello");
        server_connection->write(data, true);
      }));
  EXPECT_CALL(listener_callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(*server_read_filter, onNewConnection())
      .WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*client_read_filter, onNewConnection())
      .WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("hello"), true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> Network::FilterStatus {
        Buffer::OwnedImpl buffer("world");
        client_connection->write(buffer, true);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(*server_read_filter, onData(BufferStringEqual("world"), true));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(SslSocketTest, ShutdownWithCloseNotify) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_certificates.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(server_tls_context, factory_context_, false);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks listener_callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener = createListener(socket, listener_callbacks, runtime_,
                                                 listener_config, overload_state, *dispatcher_);
  std::shared_ptr<Network::MockReadFilter> server_read_filter(new Network::MockReadFilter());
  std::shared_ptr<Network::MockReadFilter> client_read_filter(new Network::MockReadFilter());

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::TestUtil::TestStore client_stats_store;
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                                   *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->enableHalfClose(true);
  client_connection->addReadFilter(client_read_filter);
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(listener_callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->enableHalfClose(true);
        server_connection->addReadFilter(server_read_filter);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(listener_callbacks, recordConnectionsAcceptedOnSocketEvent(_));
  EXPECT_CALL(*server_read_filter, onNewConnection());
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        Buffer::OwnedImpl data("hello");
        server_connection->write(data, true);
        EXPECT_EQ(data.length(), 0);
      }));

  EXPECT_CALL(*client_read_filter, onNewConnection())
      .WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("hello"), true))
      .WillOnce(Invoke([&](Buffer::Instance& read_buffer, bool) -> Network::FilterStatus {
        read_buffer.drain(read_buffer.length());
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        return Network::FilterStatus::StopIteration;
      }));
  EXPECT_CALL(*server_read_filter, onData(_, true));

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(SslSocketTest, ShutdownWithoutCloseNotify) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_certificates.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(server_tls_context, factory_context_, false);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks listener_callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener = createListener(socket, listener_callbacks, runtime_,
                                                 listener_config, overload_state, *dispatcher_);
  std::shared_ptr<Network::MockReadFilter> server_read_filter(new Network::MockReadFilter());
  std::shared_ptr<Network::MockReadFilter> client_read_filter(new Network::MockReadFilter());

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::TestUtil::TestStore client_stats_store;
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                                   *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->enableHalfClose(true);
  client_connection->addReadFilter(client_read_filter);
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(listener_callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->enableHalfClose(true);
        server_connection->addReadFilter(server_read_filter);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(listener_callbacks, recordConnectionsAcceptedOnSocketEvent(_));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        Buffer::OwnedImpl data("hello");
        server_connection->write(data, false);
        EXPECT_EQ(data.length(), 0);
        // Calling close(FlushWrite) in onEvent() callback results in PostIoAction::Close,
        // after which the connection is closed without write ready event being delivered,
        // and with all outstanding data (here, "hello") being lost.
      }));

  EXPECT_CALL(*client_read_filter, onNewConnection())
      .WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("hello"), false))
      .WillOnce(Invoke([&](Buffer::Instance& read_buffer, bool) -> Network::FilterStatus {
        read_buffer.drain(read_buffer.length());
        // Close without sending close_notify alert.
        const SslHandshakerImpl* ssl_socket =
            dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
        EXPECT_EQ(ssl_socket->state(), Ssl::SocketState::HandshakeComplete);
        SSL_set_quiet_shutdown(ssl_socket->ssl(), 1);
        client_connection->close(Network::ConnectionCloseType::FlushWrite);
        return Network::FilterStatus::StopIteration;
      }));

  EXPECT_CALL(*server_read_filter, onNewConnection())
      .WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*server_read_filter, onData(BufferStringEqual(""), true))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> Network::FilterStatus {
        // Close without sending close_notify alert.
        const SslHandshakerImpl* ssl_socket =
            dynamic_cast<const SslHandshakerImpl*>(server_connection->ssl().get());
        EXPECT_EQ(ssl_socket->state(), Ssl::SocketState::HandshakeComplete);
        SSL_set_quiet_shutdown(ssl_socket->ssl(), 1);
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        return Network::FilterStatus::StopIteration;
      }));

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(SslSocketTest, ClientAuthMultipleCAs) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_certificates.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(server_tls_context, factory_context_, false);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime_, listener_config, overload_state, *dispatcher_);

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::TestUtil::TestStore client_stats_store;
  auto ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                            *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  // Verify that server sent list with 2 acceptable client certificate CA names.
  const SslHandshakerImpl* ssl_socket =
      dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
  SSL_set_cert_cb(
      ssl_socket->ssl(),
      [](SSL* ssl, void*) -> int {
        STACK_OF(X509_NAME)* list = SSL_get_client_CA_list(ssl);
        EXPECT_NE(nullptr, list);
        EXPECT_EQ(2U, sk_X509_NAME_num(list));
        return 1;
      },
      nullptr);

  client_connection->connect();

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, server_stats_store.counter("ssl.handshake").value());
}

namespace {

// Test connecting with a client to server1, then trying to reuse the session on server2
void testTicketSessionResumption(const std::string& server_ctx_yaml1,
                                 const std::vector<std::string>& server_names1,
                                 const std::string& server_ctx_yaml2,
                                 const std::vector<std::string>& server_names2,
                                 const std::string& client_ctx_yaml, bool expect_reuse,
                                 const Network::Address::IpVersion ip_version,
                                 const uint32_t expected_lifetime_hint = 0) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      transport_socket_factory_context;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);

  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system);
  NiceMock<Runtime::MockLoader> runtime;
  ON_CALL(transport_socket_factory_context.server_context_, api())
      .WillByDefault(ReturnRef(*server_api));

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context1;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml1), server_tls_context1);
  auto server_cfg1 = *ServerContextConfigImpl::create(server_tls_context1,
                                                      transport_socket_factory_context, false);

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml2), server_tls_context2);
  auto server_cfg2 = *ServerContextConfigImpl::create(server_tls_context2,
                                                      transport_socket_factory_context, false);
  auto server_ssl_socket_factory1 = *ServerSslSocketFactory::create(
      std::move(server_cfg1), manager, *server_stats_store.rootScope(), server_names1);
  auto server_ssl_socket_factory2 = *ServerSslSocketFactory::create(
      std::move(server_cfg2), manager, *server_stats_store.rootScope(), server_names2);

  auto socket1 = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(ip_version));
  auto socket2 = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(ip_version));
  NiceMock<Network::MockTcpListenerCallbacks> callbacks;
  Event::DispatcherPtr dispatcher(server_api->allocateDispatcher("test_thread"));
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener1 =
      createListener(socket1, callbacks, runtime, listener_config, overload_state,
                     server_api->randomGenerator(), *dispatcher);
  Network::ListenerPtr listener2 =
      createListener(socket2, callbacks, runtime, listener_config, overload_state,
                     server_api->randomGenerator(), *dispatcher);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), client_tls_context);

  Stats::TestUtil::TestStore client_stats_store;
  Api::ApiPtr client_api = Api::createApiForTest(client_stats_store, time_system);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      client_factory_context;
  ON_CALL(client_factory_context.server_context_, api()).WillByDefault(ReturnRef(*client_api));

  auto client_cfg = *ClientContextConfigImpl::create(client_tls_context, client_factory_context);
  auto ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                            *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
      socket1->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  SSL_SESSION* ssl_session = nullptr;
  Network::ConnectionPtr server_connection;
  StreamInfo::StreamInfoImpl stream_info(time_system, nullptr,
                                         StreamInfo::FilterState::LifeSpan::Connection);
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        Network::DownstreamTransportSocketFactory& tsf =
            socket->connectionInfoProvider().localAddress() ==
                    socket1->connectionInfoProvider().localAddress()
                ? *server_ssl_socket_factory1
                : *server_ssl_socket_factory2;
        server_connection = dispatcher->createServerConnection(
            std::move(socket), tsf.createDownstreamTransportSocket(), stream_info);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        const SslHandshakerImpl* ssl_socket =
            dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
        ssl_session = SSL_get1_session(ssl_socket->ssl());
        EXPECT_TRUE(SSL_SESSION_is_resumable(ssl_session));
        if (expected_lifetime_hint) {
          auto lifetime_hint = SSL_SESSION_get_ticket_lifetime_hint(ssl_session);
          EXPECT_TRUE(lifetime_hint <= expected_lifetime_hint);
        }
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher->exit();
      }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(0UL, client_stats_store.counter("ssl.session_reused").value());

  client_connection = dispatcher->createClientConnection(
      socket2->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  const SslHandshakerImpl* ssl_socket =
      dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
  SSL_set_session(ssl_socket->ssl(), ssl_session);
  SSL_SESSION_free(ssl_session);

  client_connection->connect();

  Network::MockConnectionCallbacks server_connection_callbacks;
  StreamInfo::StreamInfoImpl stream_info2(time_system, nullptr,
                                          StreamInfo::FilterState::LifeSpan::Connection);
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        Network::DownstreamTransportSocketFactory& tsf =
            socket->connectionInfoProvider().localAddress() ==
                    socket1->connectionInfoProvider().localAddress()
                ? *server_ssl_socket_factory1
                : *server_ssl_socket_factory2;
        server_connection = dispatcher->createServerConnection(
            std::move(socket), tsf.createDownstreamTransportSocket(), stream_info2);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  // Different tests have different order of whether client or server gets Connected event
  // first, so always wait until both have happened.
  size_t connect_count = 0;
  auto connect_second_time = [&connect_count, &dispatcher, &server_connection, &client_connection,
                              expect_reuse]() {
    connect_count++;
    if (connect_count == 2) {
      if (expect_reuse) {
        EXPECT_NE(EMPTY_STRING, server_connection->ssl()->sessionId());
        EXPECT_EQ(server_connection->ssl()->sessionId(), client_connection->ssl()->sessionId());
      } else {
        EXPECT_EQ(EMPTY_STRING, server_connection->ssl()->sessionId());
      }
      client_connection->close(Network::ConnectionCloseType::NoFlush);
      server_connection->close(Network::ConnectionCloseType::NoFlush);
      dispatcher->exit();
    }
  };

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(expect_reuse ? 1UL : 0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(expect_reuse ? 1UL : 0UL, client_stats_store.counter("ssl.session_reused").value());
}

void testSupportForSessionResumption(const std::string& server_ctx_yaml,
                                     const std::string& client_ctx_yaml, bool expect_stateless,
                                     bool expect_stateful,
                                     const Network::Address::IpVersion ip_version) {
  Event::SimulatedTimeSystem time_system;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      transport_socket_factory_context;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);

  Stats::IsolatedStoreImpl server_stats_store;
  Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system);
  NiceMock<Runtime::MockLoader> runtime;
  ON_CALL(transport_socket_factory_context.server_context_, api())
      .WillByDefault(ReturnRef(*server_api));

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg =
      *ServerContextConfigImpl::create(server_tls_context, transport_socket_factory_context, false);

  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), {});
  auto tcp_socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(ip_version));
  NiceMock<Network::MockTcpListenerCallbacks> callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Event::DispatcherPtr dispatcher(server_api->allocateDispatcher("test_thread"));
  Network::ListenerPtr listener =
      createListener(tcp_socket, callbacks, runtime, listener_config, overload_state,
                     server_api->randomGenerator(), *dispatcher);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), client_tls_context);

  Stats::IsolatedStoreImpl client_stats_store;
  Api::ApiPtr client_api = Api::createApiForTest(client_stats_store, time_system);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      client_factory_context;
  ON_CALL(client_factory_context.server_context_, api()).WillByDefault(ReturnRef(*client_api));

  auto client_cfg = *ClientContextConfigImpl::create(client_tls_context, client_factory_context);
  auto ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                            *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
      tcp_socket->connectionInfoProvider().localAddress(),
      Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  StreamInfo::StreamInfoImpl stream_info(time_system, nullptr,
                                         StreamInfo::FilterState::LifeSpan::Connection);
  Network::ConnectionPtr server_connection;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info);

        const SslHandshakerImpl* ssl_socket =
            dynamic_cast<const SslHandshakerImpl*>(server_connection->ssl().get());
        SSL* server_ssl_socket = ssl_socket->ssl();
        SSL_CTX* server_ssl_context = SSL_get_SSL_CTX(server_ssl_socket);
        if (expect_stateless) {
          EXPECT_EQ(0, (SSL_CTX_get_options(server_ssl_context) & SSL_OP_NO_TICKET));
        } else {
          EXPECT_EQ(SSL_OP_NO_TICKET, (SSL_CTX_get_options(server_ssl_context) & SSL_OP_NO_TICKET));
        }
        if (expect_stateful) {
          EXPECT_EQ(SSL_SESS_CACHE_SERVER,
                    (SSL_CTX_get_session_cache_mode(server_ssl_context) & SSL_SESS_CACHE_SERVER));
        } else {
          EXPECT_EQ(SSL_SESS_CACHE_OFF, SSL_CTX_get_session_cache_mode(server_ssl_context));
        }
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher->exit();
      }));

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  dispatcher->run(Event::Dispatcher::RunType::Block);
}

} // namespace

TEST_P(SslSocketTest, TicketSessionResumption) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml, {}, server_ctx_yaml, {}, client_ctx_yaml, true,
                              version_);
}

TEST_P(SslSocketTest, TicketSessionResumptionCustomTimeout) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_0
      tls_maximum_protocol_version: TLSv1_2
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  session_timeout: 2307s
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml, {}, server_ctx_yaml, {}, client_ctx_yaml, true,
                              version_, 2307);
}

TEST_P(SslSocketTest, TicketSessionResumptionWithClientCA) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  testTicketSessionResumption(server_ctx_yaml, {}, server_ctx_yaml, {}, client_ctx_yaml, true,
                              version_);
}

TEST_P(SslSocketTest, TicketSessionResumptionRotateKey) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_b"
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, true,
                              version_);
}

TEST_P(SslSocketTest, TicketSessionResumptionWrongKey) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_b"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              version_);
}

// Sessions cannot be resumed even though the server certificates are the same,
// because of the different SNI requirements.
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerNames) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  std::vector<std::string> server_names1 = {"server1.example.com"};

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, server_names1, server_ctx_yaml2, {},
                              client_ctx_yaml, false, version_);
}

// Sessions cannot be resumed even though the server certificates are the same,
// because of the different `verify_certificate_hash` settings.
TEST_P(SslSocketTest, TicketSessionResumptionDifferentVerifyCertHash) {
  const std::string server_ctx_yaml1 = absl::StrCat(R"EOF(
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      verify_certificate_hash:
        - ")EOF",
                                                    TEST_SAN_URI_CERT_256_HASH, "\"");

  const std::string server_ctx_yaml2 = absl::StrCat(R"EOF(
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      verify_certificate_hash:
        - "0000000000000000000000000000000000000000000000000000000000000000"
        - ")EOF",
                                                    TEST_SAN_URI_CERT_256_HASH, "\"");

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml1, {}, client_ctx_yaml, true,
                              version_);
  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              version_);
}

// Sessions cannot be resumed even though the server certificates are the same,
// because of the different `verify_certificate_spki` settings.
TEST_P(SslSocketTest, TicketSessionResumptionDifferentVerifyCertSpki) {
  const std::string server_ctx_yaml1 = absl::StrCat(R"EOF(
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      verify_certificate_spki:
        - ")EOF",
                                                    TEST_SAN_URI_CERT_SPKI, "\"");

  const std::string server_ctx_yaml2 = absl::StrCat(R"EOF(
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      verify_certificate_spki:
        - "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A="
        - ")EOF",
                                                    TEST_SAN_URI_CERT_SPKI, "\"");

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml1, {}, client_ctx_yaml, true,
                              version_);
  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              version_);
}

// Sessions cannot be resumed even though the server certificates are the same,
// because of the different `match_subject_alt_names` settings.
TEST_P(SslSocketTest, TicketSessionResumptionDifferentMatchSAN) {
  const std::string server_ctx_yaml1 = R"EOF(
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      match_subject_alt_names:
        - exact: "spiffe://lyft.com/test-team"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      match_subject_alt_names:
        - prefix: "spiffe://lyft.com/test-team"
")EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml1, {}, client_ctx_yaml, true,
                              version_);
  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              version_);
}

// Sessions can be resumed because the server certificates are different but the CN/SANs and
// issuer are identical
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerCert) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns2_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, true,
                              version_);
}

// Sessions cannot be resumed because the server certificates are different, CN/SANs are identical,
// but the issuer is different.
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerCertIntermediateCA) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              version_);
}

// Sessions cannot be resumed because the server certificates are different and the SANs
// are not identical
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerCertDifferentSAN) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              version_);
}

TEST_P(SslSocketTest, SessionResumptionDisabled) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  disable_stateless_session_resumption: true
  disable_stateful_session_resumption: true
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testSupportForSessionResumption(server_ctx_yaml, client_ctx_yaml, false, false, version_);
}

TEST_P(SslSocketTest, StatelessSessionResumptionDisabled) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  disable_stateless_session_resumption: true
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testSupportForSessionResumption(server_ctx_yaml, client_ctx_yaml, false, true, version_);
}

TEST_P(SslSocketTest, StatefulSessionResumptionDisabled) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  disable_stateful_session_resumption: true
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testSupportForSessionResumption(server_ctx_yaml, client_ctx_yaml, true, false, version_);
}

TEST_P(SslSocketTest, SessionResumptionEnabledExplicitly) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  disable_stateless_session_resumption: false
  disable_stateful_session_resumption: false
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testSupportForSessionResumption(server_ctx_yaml, client_ctx_yaml, true, true, version_);
}

TEST_P(SslSocketTest, SessionResumptionEnabledByDefault) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testSupportForSessionResumption(server_ctx_yaml, client_ctx_yaml, true, true, version_);
}

// Test that if two listeners use the same cert and session ticket key, but
// different client CA, that sessions cannot be resumed.
TEST_P(SslSocketTest, ClientAuthCrossListenerSessionResumption) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  const std::string server2_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"
  require_client_certificate: true
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context1;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), tls_context1);
  auto server_cfg = *ServerContextConfigImpl::create(tls_context1, factory_context_, false);
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server2_ctx_yaml), tls_context2);
  auto server2_cfg = *ServerContextConfigImpl::create(tls_context2, factory_context_, false);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});
  auto server2_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server2_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  auto socket2 = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime_, listener_config, overload_state, *dispatcher_);
  Network::ListenerPtr listener2 =
      createListener(socket2, callbacks, runtime_, listener_config, overload_state, *dispatcher_);
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);

  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::TestUtil::TestStore client_stats_store;
  auto ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                            *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  SSL_SESSION* ssl_session = nullptr;
  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& accepted_socket) -> void {
        Network::DownstreamTransportSocketFactory& tsf =
            accepted_socket->connectionInfoProvider().localAddress() ==
                    socket->connectionInfoProvider().localAddress()
                ? *server_ssl_socket_factory
                : *server2_ssl_socket_factory;
        server_connection = dispatcher_->createServerConnection(
            std::move(accepted_socket), tsf.createDownstreamTransportSocket(), stream_info_);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        const SslHandshakerImpl* ssl_socket =
            dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
        ssl_session = SSL_get1_session(ssl_socket->ssl());
        EXPECT_TRUE(SSL_SESSION_is_resumable(ssl_session));
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, server_stats_store.counter("ssl.handshake").value());
  EXPECT_EQ(1UL, client_stats_store.counter("ssl.handshake").value());

  client_connection = dispatcher_->createClientConnection(
      socket2->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  const SslHandshakerImpl* ssl_socket =
      dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
  SSL_set_session(ssl_socket->ssl(), ssl_session);
  SSL_SESSION_free(ssl_session);

  client_connection->connect();

  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& accepted_socket) -> void {
        Network::DownstreamTransportSocketFactory& tsf =
            accepted_socket->connectionInfoProvider().localAddress() ==
                    socket->connectionInfoProvider().localAddress()
                ? *server_ssl_socket_factory
                : *server2_ssl_socket_factory;
        server_connection = dispatcher_->createServerConnection(
            std::move(accepted_socket), tsf.createDownstreamTransportSocket(), stream_info_);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, client_stats_store.counter("ssl.connection_error").value());
  EXPECT_EQ(0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(0UL, client_stats_store.counter("ssl.session_reused").value());
}

void SslSocketTest::testClientSessionResumption(const std::string& server_ctx_yaml,
                                                const std::string& client_ctx_yaml,
                                                bool expect_reuse,
                                                const Network::Address::IpVersion version) {
  InSequence s;

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  ContextManagerImpl manager(server_factory_context);

  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system_);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      transport_socket_factory_context;
  ON_CALL(transport_socket_factory_context.server_context_, api())
      .WillByDefault(ReturnRef(*server_api));

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_ctx_proto;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_ctx_proto);
  auto server_cfg =
      *ServerContextConfigImpl::create(server_ctx_proto, transport_socket_factory_context, false);
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version));
  NiceMock<Network::MockTcpListenerCallbacks> callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system_);
  Event::DispatcherPtr dispatcher(server_api->allocateDispatcher("test_thread"));
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime_, listener_config, overload_state, *dispatcher);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_ctx_proto;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), client_ctx_proto);

  Stats::TestUtil::TestStore client_stats_store;
  Api::ApiPtr client_api = Api::createApiForTest(client_stats_store, time_system_);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      client_factory_context;
  ON_CALL(client_factory_context.server_context_, api()).WillByDefault(ReturnRef(*client_api));

  auto client_cfg = *ClientContextConfigImpl::create(client_ctx_proto, client_factory_context);
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                                   *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  size_t connect_count = 0;
  auto connect_second_time = [&connect_count, &server_connection]() {
    if (++connect_count == 2) {
      server_connection->close(Network::ConnectionCloseType::NoFlush);
    }
  };

  size_t close_count = 0;
  auto close_second_time = [&close_count, &dispatcher]() {
    if (++close_count == 2) {
      dispatcher->exit();
    }
  };

  // WillRepeatedly doesn't work with InSequence.
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  const bool expect_tls13 =
      client_ctx_proto.common_tls_context().tls_params().tls_maximum_protocol_version() ==
          envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3 &&
      server_ctx_proto.common_tls_context().tls_params().tls_maximum_protocol_version() ==
          envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3;

  // The order of "Connected" events depends on the version of the TLS protocol (1.3 or older).
  if (expect_tls13) {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
  } else {
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
  }
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));

  dispatcher->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(0UL, client_stats_store.counter("ssl.session_reused").value());

  connect_count = 0;
  close_count = 0;

  client_connection = dispatcher->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  // WillRepeatedly doesn't work with InSequence.
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  // The order of "Connected" events depends on the version of the TLS protocol (1.3 or older),
  // and whether or not the session was successfully resumed.
  if (expect_tls13 || expect_reuse) {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
  } else {
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { connect_second_time(); }));
  }
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { close_second_time(); }));

  dispatcher->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(expect_reuse ? 1UL : 0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(expect_reuse ? 1UL : 0UL, client_stats_store.counter("ssl.session_reused").value());
}

// Test client session resumption using default settings (should be enabled).
TEST_P(SslSocketTest, ClientSessionResumptionDefault) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
)EOF";

  testClientSessionResumption(server_ctx_yaml, client_ctx_yaml, true, version_);
}

// Make sure client session resumption is not happening with TLS 1.0-1.2 when it's disabled.
TEST_P(SslSocketTest, ClientSessionResumptionDisabledTls12) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_0
      tls_maximum_protocol_version: TLSv1_2
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
  max_session_keys: 0
)EOF";

  testClientSessionResumption(server_ctx_yaml, client_ctx_yaml, false, version_);
}

// Test client session resumption with TLS 1.0-1.2.
TEST_P(SslSocketTest, ClientSessionResumptionEnabledTls12) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_0
      tls_maximum_protocol_version: TLSv1_2
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_0
      tls_maximum_protocol_version: TLSv1_2
  max_session_keys: 2
)EOF";

  testClientSessionResumption(server_ctx_yaml, client_ctx_yaml, true, version_);
}

// Make sure client session resumption is not happening with TLS 1.3 when it's disabled.
TEST_P(SslSocketTest, ClientSessionResumptionDisabledTls13) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_3
      tls_maximum_protocol_version: TLSv1_3
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_3
      tls_maximum_protocol_version: TLSv1_3
  max_session_keys: 0
)EOF";

  testClientSessionResumption(server_ctx_yaml, client_ctx_yaml, false, version_);
}

// Test client session resumption with TLS 1.3 (it's different than in older versions of TLS).
TEST_P(SslSocketTest, ClientSessionResumptionEnabledTls13) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_3
      tls_maximum_protocol_version: TLSv1_3
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      tls_minimum_protocol_version: TLSv1_3
      tls_maximum_protocol_version: TLSv1_3
  max_session_keys: 2
)EOF";

  testClientSessionResumption(server_ctx_yaml, client_ctx_yaml, true, version_);
}

TEST_P(SslSocketTest, SslError) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"
      verify_certificate_hash: "7B:0C:3F:0D:97:0E:FC:16:70:11:7A:0C:35:75:54:6B:17:AB:CF:20:D8:AA:A0:ED:87:08:0F:FB:60:4C:40:77"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, false);
  ContextManagerImpl manager(factory_context_.serverFactoryContext());
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime_, listener_config, overload_state, *dispatcher_);

  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr, nullptr);
  client_connection->connect();
  Buffer::OwnedImpl bad_data("bad_handshake_data");
  client_connection->write(bad_data, false);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, server_stats_store.counter("ssl.connection_error").value());
}

TEST_P(SslSocketTest, ProtocolVersions) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::TlsParameters* server_params =
      tls_context.mutable_common_tls_context()->mutable_tls_params();

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsParameters* client_params =
      client.mutable_common_tls_context()->mutable_tls_params();

  // Note: There aren't any valid TLSv1.0 or TLSv1.1 cipher suites enabled by default,
  // so enable them to avoid false positives.
  client_params->add_cipher_suites("ECDHE-RSA-AES128-SHA");
  server_params->add_cipher_suites("ECDHE-RSA-AES128-SHA");
  updateFilterChain(tls_context, *filter_chain);

  // Connection using defaults (client & server) succeeds, negotiating TLSv1.2.
  TestUtilOptionsV2 tls_v1_2_test_options =
      createProtocolTestOptions(listener, client, version_, "TLSv1.2");
  testUtilV2(tls_v1_2_test_options);

  // Connection using defaults (client & server) succeeds, negotiating TLSv1.2,
  // even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(tls_v1_2_test_options);
  client.set_allow_renegotiation(false);

  TestUtilOptionsV2 error_test_options(listener, client, false, version_);
  error_test_options.setExpectedServerStats("ssl.connection_error")
      .setExpectedTransportFailureReasonContains("TLSV1_ALERT_PROTOCOL_VERSION");

  // Connection using TLSv1.0 (client) and defaults (server) fails.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  testUtilV2(error_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.1 (client) and defaults (server) fails.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_1);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_1);
  testUtilV2(error_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.2 (client) and defaults (server) succeeds.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  testUtilV2(tls_v1_2_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.3 (client) and defaults (server) succeeds.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  TestUtilOptionsV2 tls_v1_3_test_options =
      createProtocolTestOptions(listener, client, version_, "TLSv1.3");
  testUtilV2(tls_v1_3_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.0-1.3 (client) and defaults (server) succeeds.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  testUtilV2(tls_v1_3_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.0 (client) and TLSv1.0-1.3 (server) succeeds.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  updateFilterChain(tls_context, *filter_chain);
  TestUtilOptionsV2 tls_v1_test_options =
      createProtocolTestOptions(listener, client, version_, "TLSv1");
  testUtilV2(tls_v1_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.3 (client) and TLSv1.0-1.3 (server) succeeds.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(tls_v1_3_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  TestUtilOptionsV2 unsupported_protocol_test_options(listener, client, false, version_);
  unsupported_protocol_test_options.setExpectedServerStats("ssl.connection_error")
      .setExpectedTransportFailureReasonContains("UNSUPPORTED_PROTOCOL");

  // Connection using defaults (client) and TLSv1.0 (server) fails.
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(unsupported_protocol_test_options);
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  // Connection using defaults (client) and TLSv1.1 (server) fails.
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_1);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_1);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(unsupported_protocol_test_options);
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  // Connection using defaults (client) and TLSv1.2 (server) succeeds.
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(tls_v1_2_test_options);
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  // Connection using defaults (client) and TLSv1.3 (server) fails.
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(error_test_options);
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  // Connection using defaults (client) and TLSv1.0-1.3 (server) succeeds.
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(tls_v1_2_test_options);
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.0-TLSv1.3 (client) and TLSv1.0 (server) succeeds.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(tls_v1_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();

  // Connection using TLSv1.0-TLSv1.3 (client) and TLSv1.3 (server) succeeds.
  client_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
  client_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  server_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  server_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(tls_v1_3_test_options);
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();
  server_params->clear_tls_minimum_protocol_version();
  server_params->clear_tls_maximum_protocol_version();
}

TEST_P(SslSocketTest, ALPN) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::CommonTlsContext* server_ctx =
      tls_context.mutable_common_tls_context();
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::CommonTlsContext* client_ctx =
      client.mutable_common_tls_context();

  // Connection using defaults (client & server) succeeds, no ALPN is negotiated.
  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options);

  // Connection using defaults (client & server) succeeds, no ALPN is negotiated,
  // even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
  client.set_allow_renegotiation(false);

  // Client connects without ALPN to a server with "test" ALPN, no ALPN is negotiated.
  server_ctx->add_alpn_protocols("test");
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(test_options);
  server_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server without ALPN, no ALPN is negotiated.
  client_ctx->add_alpn_protocols("test");
  testUtilV2(test_options);

  client_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server with "test" ALPN, "test" ALPN is negotiated.
  client_ctx->add_alpn_protocols("test");
  server_ctx->add_alpn_protocols("test");
  updateFilterChain(tls_context, *filter_chain);
  test_options.setExpectedALPNProtocol("test");
  testUtilV2(test_options);
  test_options.setExpectedALPNProtocol("");
  client_ctx->clear_alpn_protocols();
  server_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server with "test" ALPN, "test" ALPN is negotiated,
  // even with client renegotiation.
  client.set_allow_renegotiation(true);
  client_ctx->add_alpn_protocols("test");
  server_ctx->add_alpn_protocols("test");
  updateFilterChain(tls_context, *filter_chain);
  test_options.setExpectedALPNProtocol("test");
  testUtilV2(test_options);
  test_options.setExpectedALPNProtocol("");
  client.set_allow_renegotiation(false);
  client_ctx->clear_alpn_protocols();
  server_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server with "test2" ALPN, no ALPN is negotiated.
  client_ctx->add_alpn_protocols("test");
  server_ctx->add_alpn_protocols("test2");
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(test_options);
  client_ctx->clear_alpn_protocols();
  server_ctx->clear_alpn_protocols();

  // Client attempts to configure ALPN that is too large.
  client_ctx->add_alpn_protocols(std::string(100000, 'a'));
  EXPECT_THROW_WITH_MESSAGE(testUtilV2(test_options), EnvoyException,
                            "Invalid ALPN protocol string");
}

TEST_P(SslSocketTest, CipherSuites) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;

  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsParameters* client_params =
      client.mutable_common_tls_context()->mutable_tls_params();

  // Connection using defaults (client & server) succeeds.
  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options);

  // Connection using defaults (client & server) succeeds, even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
  client.set_allow_renegotiation(false);

  // Client connects with one of the supported cipher suites, connection succeeds.
  std::string common_cipher_suite = "ECDHE-RSA-CHACHA20-POLY1305";
  client_params->add_cipher_suites(common_cipher_suite);
  envoy::extensions::transport_sockets::tls::v3::TlsParameters* server_params =
      tls_context.mutable_common_tls_context()->mutable_tls_params();
  server_params->add_cipher_suites(common_cipher_suite);
  server_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  updateFilterChain(tls_context, *filter_chain);
  TestUtilOptionsV2 cipher_test_options(listener, client, true, version_);
  cipher_test_options.setExpectedCiphersuite(common_cipher_suite);
  std::string stats = "ssl.ciphers." + common_cipher_suite;
  cipher_test_options.setExpectedServerStats(stats).setExpectedClientStats(stats);
  testUtilV2(cipher_test_options);
  client_params->clear_cipher_suites();
  server_params->clear_cipher_suites();

  // Client connects with unsupported cipher suite, connection fails.
  client_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  client_params->add_cipher_suites("ECDHE-ECDSA-AES128-SHA");
  client_params->add_cipher_suites("ECDHE-RSA-AES128-SHA");
  client_params->add_cipher_suites("AES128-SHA");
  client_params->add_cipher_suites("AES128-GCM-SHA256");
  client_params->add_cipher_suites("ECDHE-ECDSA-AES256-SHA");
  client_params->add_cipher_suites("ECDHE-RSA-AES256-SHA");
  client_params->add_cipher_suites("AES256-SHA");
  client_params->add_cipher_suites("AES256-GCM-SHA384");
  server_params->add_cipher_suites("ECDHE-RSA-CHACHA20-POLY1305");
  updateFilterChain(tls_context, *filter_chain);
  TestUtilOptionsV2 error_test_options(listener, client, false, version_);
  error_test_options.setExpectedServerStats("ssl.connection_error");
  testUtilV2(error_test_options);
  client_params->clear_cipher_suites();
  server_params->clear_cipher_suites();

  // Client connects to a server offering only deprecated cipher suites, connection fails.
  server_params->add_cipher_suites("ECDHE-RSA-AES128-SHA");
  updateFilterChain(tls_context, *filter_chain);
  error_test_options.setExpectedServerStats("ssl.connection_error");
  testUtilV2(error_test_options);
  server_params->clear_cipher_suites();
  updateFilterChain(tls_context, *filter_chain);
  // Verify that ECDHE-RSA-CHACHA20-POLY1305 is not offered by default in FIPS builds.
  client_params->add_cipher_suites(common_cipher_suite);
#ifdef BORINGSSL_FIPS
  testUtilV2(error_test_options);
#else
  testUtilV2(cipher_test_options);
#endif
  client_params->clear_cipher_suites();
}

TEST_P(SslSocketTest, EcdhCurves) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  envoy::extensions::transport_sockets::tls::v3::TlsParameters* server_params =
      tls_context.mutable_common_tls_context()->mutable_tls_params();
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  envoy::extensions::transport_sockets::tls::v3::TlsParameters* client_params =
      client.mutable_common_tls_context()->mutable_tls_params();

  // Connection using defaults (client & server) succeeds.
  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options);

  // Connection using defaults (client & server) succeeds, even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(test_options);
  client.set_allow_renegotiation(false);

  // Client connects with one of the supported ECDH curves, connection succeeds.
  client_params->add_ecdh_curves("X25519");
  server_params->add_ecdh_curves("X25519");
  server_params->add_ecdh_curves("P-256");
  server_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  updateFilterChain(tls_context, *filter_chain);
  TestUtilOptionsV2 ecdh_curves_test_options(listener, client, true, version_);
  std::string stats = "ssl.curves.X25519";
  ecdh_curves_test_options.setExpectedServerStats(stats).setExpectedClientStats(stats);
  testUtilV2(ecdh_curves_test_options);
  client_params->clear_ecdh_curves();
  server_params->clear_ecdh_curves();
  server_params->clear_cipher_suites();

  // Client connects with unsupported ECDH curve, connection fails.
  client_params->add_ecdh_curves("X25519");
  server_params->add_ecdh_curves("P-256");
  server_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  updateFilterChain(tls_context, *filter_chain);
  TestUtilOptionsV2 error_test_options(listener, client, false, version_);
  error_test_options.setExpectedServerStats("ssl.connection_error");
  testUtilV2(error_test_options);

  client_params->clear_ecdh_curves();
  server_params->clear_ecdh_curves();
  server_params->clear_cipher_suites();

  // Verify that X25519 is not offered by default in FIPS builds.
  client_params->add_ecdh_curves("X25519");
  server_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  updateFilterChain(tls_context, *filter_chain);
#ifdef BORINGSSL_FIPS
  testUtilV2(error_test_options);
#else
  testUtilV2(ecdh_curves_test_options);
#endif
  client_params->clear_ecdh_curves();
  server_params->clear_cipher_suites();
}

TEST_P(SslSocketTest, SignatureAlgorithms) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();

  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  // Server ECDSA certificate.
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"));
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  // Client RSA certificate.
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"));

  // Connection using defaults (client & server) succeeds.
  TestUtilOptionsV2 algorithm_test_options(listener, client, true, version_);
  algorithm_test_options.setExpectedClientCertUri("spiffe://lyft.com/test-team")
      .setExpectedServerStats("ssl.sigalgs.rsa_pss_rsae_sha256")
      .setExpectedClientStats("ssl.sigalgs.ecdsa_secp256r1_sha256");
  testUtilV2(algorithm_test_options);

  // Connection using defaults (client & server) succeeds, even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(algorithm_test_options);
  client.set_allow_renegotiation(false);
}

TEST_P(SslSocketTest, SetSignatureAlgorithms) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      signature_algorithms:
      - rsa_pss_rsae_sha256
      - ecdsa_secp256r1_sha256
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = *ServerContextConfigImpl::create(server_tls_context, factory_context_, false);
  ContextManagerImpl manager(factory_context_.serverFactoryContext());
  Stats::TestUtil::TestStore server_stats_store;
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *server_stats_store.rootScope(), std::vector<std::string>{});

  auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(version_));
  Network::MockTcpListenerCallbacks callbacks;
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  Network::ListenerPtr listener =
      createListener(socket, callbacks, runtime_, listener_config, overload_state, *dispatcher_);

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      signature_algorithms:
      - rsa_pkcs1_sha256
      - rsa_pss_rsae_sha256
      - ecdsa_secp256r1_sha256
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::TestUtil::TestStore client_stats_store;
  auto ssl_socket_factory = *ClientSslSocketFactory::create(std::move(client_cfg), manager,
                                                            *client_stats_store.rootScope());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);

  // Verify that server sent configured signature algorithms.
  const SslHandshakerImpl* client_socket =
      dynamic_cast<const SslHandshakerImpl*>(client_connection->ssl().get());
  SSL_set_cert_cb(
      client_socket->ssl(),
      [](SSL* ssl, void*) -> int {
        const uint16_t* peer_sigalgs;
        size_t num_peer_sigalgs = SSL_get0_peer_verify_algorithms(ssl, &peer_sigalgs);
        EXPECT_EQ(2, num_peer_sigalgs);
        EXPECT_EQ(SSL_SIGN_RSA_PSS_RSAE_SHA256, peer_sigalgs[0]);
        EXPECT_EQ(SSL_SIGN_ECDSA_SECP256R1_SHA256, peer_sigalgs[1]);
        return 1;
      },
      nullptr);

  client_connection->connect();

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory->createDownstreamTransportSocket(),
            stream_info_);

        // Verify that client sent configured signature algorithms.
        const SslHandshakerImpl* server_socket =
            dynamic_cast<const SslHandshakerImpl*>(server_connection->ssl().get());
        SSL_set_cert_cb(
            server_socket->ssl(),
            [](SSL* ssl, void*) -> int {
              const uint16_t* peer_sigalgs;
              size_t num_peer_sigalgs = SSL_get0_peer_verify_algorithms(ssl, &peer_sigalgs);
              EXPECT_EQ(3, num_peer_sigalgs);
              EXPECT_EQ(SSL_SIGN_RSA_PKCS1_SHA256, peer_sigalgs[0]);
              EXPECT_EQ(SSL_SIGN_RSA_PSS_RSAE_SHA256, peer_sigalgs[1]);
              EXPECT_EQ(SSL_SIGN_ECDSA_SECP256R1_SHA256, peer_sigalgs[2]);
              return 1;
            },
            nullptr);

        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(callbacks, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, server_stats_store.counter("ssl.handshake").value());
}

TEST_P(SslSocketTest, SetSignatureAlgorithmsFailure) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      signature_algorithms:
      - invalid_sigalg_name
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      signature_algorithms:
      - invalid_sigalg_name
)EOF";

  TestUtilOptions options(client_ctx_yaml, server_ctx_yaml, false, version_);
  EXPECT_THROW_WITH_MESSAGE(testUtil(options), EnvoyException,
                            "Failed to initialize TLS signature algorithms invalid_sigalg_name");
}

TEST_P(SslSocketTest, RevokedCertificate) {

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";

  // This should fail, since the certificate has been revoked.
  const std::string revoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
)EOF";

  TestUtilOptions revoked_test_options(revoked_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));

  // This should succeed, since the cert isn't revoked.
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns2_key.pem"
)EOF";

  TestUtilOptions successful_test_options(successful_client_ctx_yaml, server_ctx_yaml, true,
                                          version_);
  testUtil(successful_test_options.setExpectedSerialNumber(TEST_SAN_DNS2_CERT_SERIAL));
}

TEST_P(SslSocketTest, RevokedCertificateCRLInTrustedCA) {

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert_with_crl.pem"
)EOF";

  // This should fail, since the certificate has been revoked.
  const std::string revoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
)EOF";

  TestUtilOptions revoked_test_options(revoked_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));

  // This should succeed, since the cert isn't revoked.
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns2_key.pem"
)EOF";
  TestUtilOptions successful_test_options(successful_client_ctx_yaml, server_ctx_yaml, true,
                                          version_);
  testUtil(successful_test_options.setExpectedSerialNumber(TEST_SAN_DNS2_CERT_SERIAL));
}

TEST_P(SslSocketTest, RevokedIntermediateCertificate) {

  // This should succeed, since the crl chain is complete.
  //
  // Trust chain contains:
  //  - Root authority certificate (i.e., ca_cert.pem)
  //  - Intermediate authority certificate (i.e., intermediate_ca_cert.pem)
  //
  // Certificate revocation list contains:
  //  - Root authority certificate revocation list (i.e., ca_cert.crl)
  //  - Intermediate authority certificate revocation list (i.e., intermediate_ca_cert.crl)
  const std::string complete_server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert_chain.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert_chain.crl"
)EOF";

  // This should fail, since the crl chain is incomplete.
  //
  // Trust chain contains:
  //  - Root authority certificate (i.e., ca_cert.pem)
  //  - Intermediate authority certificate (i.e., intermediate_ca_cert.pem)
  //
  // Certificate revocation list contains:
  //  - Root authority certificate revocation list (i.e., ca_cert.crl)
  //
  // Certificate revocation list omits:
  //  - Root authority certificate revocation list (i.e., ca_cert.crl)
  const std::string incomplete_server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert_chain.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert.crl"
)EOF";

  // This should fail, since the certificate has been revoked.
  const std::string revoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
)EOF";

  // This should succeed, since the certificate has not been revoked.
  const std::string unrevoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns4_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns4_key.pem"
)EOF";

  // Ensure that incomplete crl chains fail with revoked certificates.
  TestUtilOptions incomplete_revoked_test_options(revoked_client_ctx_yaml,
                                                  incomplete_server_ctx_yaml, false, version_);
  testUtil(incomplete_revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));

  // Ensure that incomplete crl chains fail with unrevoked certificates.
  TestUtilOptions incomplete_unrevoked_test_options(unrevoked_client_ctx_yaml,
                                                    incomplete_server_ctx_yaml, false, version_);
  testUtil(incomplete_unrevoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_UNABLE_TO_GET_CRL));

  // Ensure that complete crl chains fail with revoked certificates.
  TestUtilOptions complete_revoked_test_options(revoked_client_ctx_yaml, complete_server_ctx_yaml,
                                                false, version_);
  testUtil(complete_revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));

  // Ensure that complete crl chains succeed with unrevoked certificates.
  TestUtilOptions complete_unrevoked_test_options(unrevoked_client_ctx_yaml,
                                                  complete_server_ctx_yaml, true, version_);
  testUtil(complete_unrevoked_test_options.setExpectedSerialNumber(TEST_SAN_DNS4_CERT_SERIAL));
}

TEST_P(SslSocketTest, RevokedIntermediateCertificateCRLInTrustedCA) {

  // This should succeed, since the crl chain is complete.
  //
  // Trust chain contains:
  //  - Root authority certificate (i.e., ca_cert.pem)
  //  - Root authority certificate revocation list (i.e., ca_cert.crl)
  //  - Intermediate authority certificate (i.e., intermediate_ca_cert.pem)
  //  - Intermediate authority certificate revocation list (i.e., intermediate_ca_cert.crl)
  const std::string complete_server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert_chain_with_crl_chain.pem"
)EOF";

  // This should fail, since the crl chain is incomplete.
  //
  // Trust chain contains:
  //  - Root authority certificate (i.e., ca_cert.pem)
  //  - Intermediate authority certificate (i.e., intermediate_ca_cert.pem)
  //  - Intermediate authority certificate revocation list (i.e., intermediate_ca_cert.crl)
  //
  // Trust chain omits:
  //  - Root authority certificate revocation list (i.e., ca_cert.crl)
  const std::string incomplete_server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert_chain_with_crl.pem"
)EOF";

  // This should fail, since the certificate has been revoked.
  const std::string revoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
)EOF";

  // This should succeed, since the certificate has not been revoked.
  const std::string unrevoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns4_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns4_key.pem"
)EOF";

  // Ensure that incomplete crl chains fail with revoked certificates.
  TestUtilOptions incomplete_revoked_test_options(revoked_client_ctx_yaml,
                                                  incomplete_server_ctx_yaml, false, version_);
  testUtil(incomplete_revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));

  // Ensure that incomplete crl chains fail with unrevoked certificates.
  TestUtilOptions incomplete_unrevoked_test_options(unrevoked_client_ctx_yaml,
                                                    incomplete_server_ctx_yaml, false, version_);
  testUtil(incomplete_unrevoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_UNABLE_TO_GET_CRL));

  // Ensure that complete crl chains fail with revoked certificates.
  TestUtilOptions complete_revoked_test_options(revoked_client_ctx_yaml, complete_server_ctx_yaml,
                                                false, version_);
  testUtil(complete_revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));

  // Ensure that complete crl chains succeed with unrevoked certificates.
  TestUtilOptions complete_unrevoked_test_options(unrevoked_client_ctx_yaml,
                                                  complete_server_ctx_yaml, true, version_);
  testUtil(complete_unrevoked_test_options.setExpectedSerialNumber(TEST_SAN_DNS4_CERT_SERIAL));
}

TEST_P(SslSocketTest, NotRevokedLeafCertificateOnlyLeafCRLValidation) {
  // The test checks that revoked certificate will makes the validation success even if we set
  // only_verify_leaf_cert_crl to true.
  //
  // Trust chain contains:
  //  - Root authority certificate (i.e., ca_cert.pem)
  //  - Intermediate authority certificate (i.e., intermediate_ca_cert.pem)
  //  - Intermediate authority certificate revocation list (i.e., intermediate_ca_cert.crl)
  //
  // Trust chain omits (But this test will succeed):
  //  - Root authority certificate revocation list (i.e., ca_cert.crl)
  const std::string incomplete_server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert_chain_with_crl.pem"
      only_verify_leaf_cert_crl: true
)EOF";

  // This should succeed, since the certificate has not been revoked.
  const std::string unrevoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns4_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns4_key.pem"
)EOF";

  TestUtilOptions complete_unrevoked_test_options(unrevoked_client_ctx_yaml,
                                                  incomplete_server_ctx_yaml, true, version_);
  testUtil(complete_unrevoked_test_options.setExpectedSerialNumber(TEST_SAN_DNS4_CERT_SERIAL));
}

TEST_P(SslSocketTest, RevokedLeafCertificateOnlyLeafCRLValidation) {
  // The test checks that revoked certificate will makes the validation fails even if we set
  // only_verify_leaf_cert_crl to true.
  //
  // Trust chain contains:
  //  - Root authority certificate (i.e., ca_cert.pem)
  //  - Intermediate authority certificate (i.e., intermediate_ca_cert.pem)
  //  - Intermediate authority certificate revocation list (i.e., intermediate_ca_cert.crl)
  //
  // Trust chain omits (But this test will succeed):
  //  - Root authority certificate revocation list (i.e., ca_cert.crl)
  const std::string incomplete_server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert_chain_with_crl.pem"
      only_verify_leaf_cert_crl: true
)EOF";

  // This should fail, since the certificate has been revoked.
  const std::string revoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
)EOF";

  TestUtilOptions complete_revoked_test_options(revoked_client_ctx_yaml, incomplete_server_ctx_yaml,
                                                false, version_);
  testUtil(complete_revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));
}

TEST_P(SslSocketTest, GetRequestedServerName) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  client.set_sni("lyft.com");

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedRequestedServerName("lyft.com"));
}

TEST_P(SslSocketTest, OverrideRequestedServerName) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  client.set_sni("lyft.com");

  Network::TransportSocketOptionsConstSharedPtr transport_socket_options(
      new Network::TransportSocketOptionsImpl("example.com"));

  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedRequestedServerName("example.com")
                 .setTransportSocketOptions(transport_socket_options));
}

TEST_P(SslSocketTest, OverrideRequestedServerNameWithoutSniInUpstreamTlsContext) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));
  updateFilterChain(tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;

  Network::TransportSocketOptionsConstSharedPtr transport_socket_options(
      new Network::TransportSocketOptionsImpl("example.com"));
  TestUtilOptionsV2 test_options(listener, client, true, version_);
  testUtilV2(test_options.setExpectedRequestedServerName("example.com")
                 .setTransportSocketOptions(transport_socket_options));
}

TEST_P(SslSocketTest, OverrideApplicationProtocols) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();

  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"));

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client;
  TestUtilOptionsV2 test_options(listener, client, true, version_);

  // Client connects without ALPN to a server with "test" ALPN, no ALPN is negotiated.
  envoy::extensions::transport_sockets::tls::v3::CommonTlsContext* server_ctx =
      tls_context.mutable_common_tls_context();
  server_ctx->add_alpn_protocols("test");
  updateFilterChain(tls_context, *filter_chain);
  testUtilV2(test_options);
  server_ctx->clear_alpn_protocols();
  // Override client side ALPN, "test" ALPN is used.
  server_ctx->add_alpn_protocols("test");
  updateFilterChain(tls_context, *filter_chain);
  auto transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
      "", std::vector<std::string>{}, std::vector<std::string>{"foo", "test", "bar"});

  testUtilV2(test_options.setExpectedALPNProtocol("test").setTransportSocketOptions(
      transport_socket_options));

  // Set fallback ALPN on the client side ALPN, "test" ALPN is used since no ALPN is specified
  // in the config.
  server_ctx->add_alpn_protocols("test");
  transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
      "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{"test"});
  testUtilV2(test_options.setExpectedALPNProtocol("test").setTransportSocketOptions(
      transport_socket_options));

  // With multiple fallbacks specified, a single match will match.
  transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
      "", std::vector<std::string>{}, std::vector<std::string>{},
      std::vector<std::string>{"foo", "test"});
  testUtilV2(test_options.setExpectedALPNProtocol("test").setTransportSocketOptions(
      transport_socket_options));

  // With multiple matching fallbacks specified, a single match will match.
  server_ctx->add_alpn_protocols("foo");
  transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
      "", std::vector<std::string>{}, std::vector<std::string>{},
      std::vector<std::string>{"foo", "test"});
  testUtilV2(test_options.setExpectedALPNProtocol("test").setTransportSocketOptions(
      transport_socket_options));

  // Update the client TLS config to specify ALPN. The fallback value should no longer be used.
  // Note that the server prefers "test" over "bar", but since the client only configures "bar",
  // the resulting ALPN will be "bar" even though "test" is included in the fallback.
  server_ctx->add_alpn_protocols("bar");
  updateFilterChain(tls_context, *filter_chain);
  client.mutable_common_tls_context()->add_alpn_protocols("bar");
  testUtilV2(test_options.setExpectedALPNProtocol("bar").setTransportSocketOptions(
      transport_socket_options));
}

// Validate that if downstream secrets are not yet downloaded from SDS server, Envoy creates
// NotReadySslSocket object to handle downstream connection.
TEST_P(SslSocketTest, DownstreamNotReadySslSocket) {
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;

  EXPECT_CALL(factory_context_.server_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto server_cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, false);
  EXPECT_TRUE(server_cfg->tlsCertificates().empty());
  EXPECT_FALSE(server_cfg->isReady());

  ContextManagerImpl manager(factory_context_.serverFactoryContext());
  auto server_ssl_socket_factory = *ServerSslSocketFactory::create(
      std::move(server_cfg), manager, *factory_context_.store_.rootScope(),
      std::vector<std::string>{});
  auto transport_socket = server_ssl_socket_factory->createDownstreamTransportSocket();
  EXPECT_FALSE(transport_socket->startSecureTransport());                                  // Noop
  transport_socket->configureInitialCongestionWindow(200, std::chrono::microseconds(223)); // Noop
  EXPECT_EQ(EMPTY_STRING, transport_socket->protocol());
  EXPECT_EQ(nullptr, transport_socket->ssl());
  EXPECT_EQ(true, transport_socket->canFlushClose());
  Buffer::OwnedImpl buffer;
  Network::IoResult result = transport_socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  result = transport_socket->doWrite(buffer, true);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_EQ("TLS error: Secret is not supplied by SDS", transport_socket->failureReason());
}

// Validate that if upstream secrets are not yet downloaded from SDS server, Envoy creates
// NotReadySslSocket object to handle upstream connection.
TEST_P(SslSocketTest, UpstreamNotReadySslSocket) {
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_.server_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_TRUE(client_cfg->tlsCertificates().empty());
  EXPECT_FALSE(client_cfg->isReady());

  ContextManagerImpl manager(factory_context_.serverFactoryContext());
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(
      std::move(client_cfg), manager, *factory_context_.store_.rootScope());
  auto transport_socket = client_ssl_socket_factory->createTransportSocket(nullptr, nullptr);
  EXPECT_EQ(EMPTY_STRING, transport_socket->protocol());
  EXPECT_EQ(nullptr, transport_socket->ssl());
  EXPECT_EQ(true, transport_socket->canFlushClose());
  Buffer::OwnedImpl buffer;
  Network::IoResult result = transport_socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  result = transport_socket->doWrite(buffer, true);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_EQ("TLS error: Secret is not supplied by SDS", transport_socket->failureReason());
}

TEST_P(SslSocketTest, TestTransportSocketCallback) {
  // Make MockTransportSocketCallbacks.
  Network::MockIoHandle io_handle;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  ON_CALL(callbacks, ioHandle()).WillByDefault(ReturnRef(io_handle));

  // Make SslSocket.
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  ON_CALL(factory_context_.server_context_, localInfo()).WillByDefault(ReturnRef(local_info));

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  auto client_cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);

  ContextManagerImpl manager(factory_context_.serverFactoryContext());
  auto client_ssl_socket_factory = *ClientSslSocketFactory::create(
      std::move(client_cfg), manager, *factory_context_.store_.rootScope());

  Network::TransportSocketPtr transport_socket =
      client_ssl_socket_factory->createTransportSocket(nullptr, nullptr);

  SslSocket* ssl_socket = dynamic_cast<SslSocket*>(transport_socket.get());

  // If no transport socket callbacks have been set, this method should return nullptr.
  EXPECT_EQ(ssl_socket->transportSocketCallbacks(), nullptr);

  // Otherwise, it should return a pointer to the set callbacks object.
  ssl_socket->setTransportSocketCallbacks(callbacks);
  EXPECT_EQ(ssl_socket->transportSocketCallbacks(), &callbacks);
}

class SslReadBufferLimitTest : public SslSocketTest {
protected:
  void initialize() {
    TestUtility::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml_),
                              downstream_tls_context_);
    auto server_cfg =
        *ServerContextConfigImpl::create(downstream_tls_context_, factory_context_, false);
    manager_ = std::make_unique<ContextManagerImpl>(factory_context_.serverFactoryContext());
    server_ssl_socket_factory_ = *ServerSslSocketFactory::create(std::move(server_cfg), *manager_,
                                                                 *server_stats_store_.rootScope(),
                                                                 std::vector<std::string>{});

    socket_ = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
        Network::Test::getCanonicalLoopbackAddress(version_));
    NiceMock<Network::MockListenerConfig> listener_config;
    Server::ThreadLocalOverloadStateOptRef overload_state;
    listener_ = createListener(socket_, listener_callbacks_, runtime_, listener_config,
                               overload_state, *dispatcher_);

    TestUtility::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml_), upstream_tls_context_);
    auto client_cfg = *ClientContextConfigImpl::create(upstream_tls_context_, factory_context_);

    client_ssl_socket_factory_ = *ClientSslSocketFactory::create(std::move(client_cfg), *manager_,
                                                                 *client_stats_store_.rootScope());
    auto transport_socket = client_ssl_socket_factory_->createTransportSocket(nullptr, nullptr);
    client_transport_socket_ = transport_socket.get();
    client_connection_ = dispatcher_->createClientConnection(
        socket_->connectionInfoProvider().localAddress(), source_address_,
        std::move(transport_socket), nullptr, nullptr);
    client_connection_->addConnectionCallbacks(client_callbacks_);
    client_connection_->connect();
    read_filter_ = std::make_shared<Network::MockReadFilter>();
  }

  void readBufferLimitTest(uint32_t read_buffer_limit, uint32_t expected_chunk_size,
                           uint32_t write_size, uint32_t num_writes, bool reserve_write_space) {
    initialize();

    EXPECT_CALL(listener_callbacks_, onAccept_(_))
        .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
          server_connection_ = dispatcher_->createServerConnection(
              std::move(socket), server_ssl_socket_factory_->createDownstreamTransportSocket(),
              stream_info_);
          server_connection_->setBufferLimits(read_buffer_limit);
          server_connection_->addConnectionCallbacks(server_callbacks_);
          server_connection_->addReadFilter(read_filter_);
          EXPECT_EQ("", server_connection_->nextProtocol());
          EXPECT_EQ(read_buffer_limit, server_connection_->bufferLimit());
        }));
    EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    uint32_t filter_seen = 0;

    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> Network::FilterStatus {
          EXPECT_GE(expected_chunk_size, data.length());
          filter_seen += data.length();
          data.drain(data.length());
          if (filter_seen == (write_size * num_writes)) {
            server_connection_->close(Network::ConnectionCloseType::FlushWrite);
          }
          return Network::FilterStatus::StopIteration;
        }));

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          EXPECT_EQ((write_size * num_writes), filter_seen);
          dispatcher_->exit();
        }));

    for (uint32_t i = 0; i < num_writes; i++) {
      Buffer::OwnedImpl data(std::string(write_size, 'a'));

      if (reserve_write_space) {
        data.appendSliceForTest(absl::string_view());
        ASSERT_EQ(0, data.describeSlicesForTest().back().data);
      }

      client_connection_->write(data, false);
    }

    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_EQ(0UL, server_stats_store_.counter("ssl.connection_error").value());
    EXPECT_EQ(0UL, client_stats_store_.counter("ssl.connection_error").value());
  }

  void singleWriteTest(uint32_t read_buffer_limit, uint32_t bytes_to_write) {
    MockWatermarkBuffer* client_write_buffer = nullptr;
    MockBufferFactory* factory = new StrictMock<MockBufferFactory>;
    dispatcher_ = api_->allocateDispatcher("test_thread", Buffer::WatermarkFactoryPtr{factory});

    // By default, expect 4 buffers to be created - the client and server read and write buffers.
    EXPECT_CALL(*factory, createBuffer_(_, _, _))
        .Times(4)
        .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                             std::function<void()> above_overflow) -> Buffer::Instance* {
          client_write_buffer = new MockWatermarkBuffer(below_low, above_high, above_overflow);
          return client_write_buffer;
        }))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));

    initialize();

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    EXPECT_CALL(listener_callbacks_, onAccept_(_))
        .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
          server_connection_ = dispatcher_->createServerConnection(
              std::move(socket), server_ssl_socket_factory_->createDownstreamTransportSocket(),
              stream_info_);
          server_connection_->setBufferLimits(read_buffer_limit);
          server_connection_->addConnectionCallbacks(server_callbacks_);
          server_connection_->addReadFilter(read_filter_);
          EXPECT_EQ("", server_connection_->nextProtocol());
          EXPECT_EQ(read_buffer_limit, server_connection_->bufferLimit());
        }));
    EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _)).Times(testing::AnyNumber());

    std::string data_to_write(bytes_to_write, 'a');
    Buffer::OwnedImpl buffer_to_write(data_to_write);
    std::string data_written;
    EXPECT_CALL(*client_write_buffer, move(_))
        .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                              Invoke(client_write_buffer, &MockWatermarkBuffer::baseMove)));
    EXPECT_CALL(*client_write_buffer, drain(_))
        .Times(2)
        .WillRepeatedly(Invoke([&](uint64_t n) -> void {
          client_write_buffer->baseDrain(n);
          dispatcher_->exit();
        }));
    client_connection_->write(buffer_to_write, false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    EXPECT_EQ(data_to_write, data_written);

    disconnect();
  }

  void disconnect() {
    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    client_connection_->close(Network::ConnectionCloseType::NoFlush);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  Stats::TestUtil::TestStore server_stats_store_;
  Stats::TestUtil::TestStore client_stats_store_;
  std::shared_ptr<Network::Test::TcpListenSocketImmediateListen> socket_;
  Network::MockTcpListenerCallbacks listener_callbacks_;
  const std::string server_ctx_yaml_ = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  const std::string client_ctx_yaml_ = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
)EOF";

  NiceMock<Runtime::MockLoader> runtime_;
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext downstream_tls_context_;
  std::unique_ptr<ContextManagerImpl> manager_;
  Network::DownstreamTransportSocketFactoryPtr server_ssl_socket_factory_;
  Network::ListenerPtr listener_;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext upstream_tls_context_;
  Envoy::Ssl::ClientContextSharedPtr client_ctx_;
  Network::UpstreamTransportSocketFactoryPtr client_ssl_socket_factory_;
  Network::ClientConnectionPtr client_connection_;
  Network::TransportSocket* client_transport_socket_{};
  Network::ConnectionPtr server_connection_;
  NiceMock<Network::MockConnectionCallbacks> server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  StrictMock<Network::MockConnectionCallbacks> client_callbacks_;
  Network::Address::InstanceConstSharedPtr source_address_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslReadBufferLimitTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SslReadBufferLimitTest, NoLimit) {
  readBufferLimitTest(0, 256 * 1024, 256 * 1024, 1, false);
}

TEST_P(SslReadBufferLimitTest, NoLimitReserveSpace) { readBufferLimitTest(0, 512, 512, 1, true); }

TEST_P(SslReadBufferLimitTest, NoLimitSmallWrites) {
  readBufferLimitTest(0, 256 * 1024, 1, 256 * 1024, false);
}

TEST_P(SslReadBufferLimitTest, SomeLimit) {
  readBufferLimitTest(32 * 1024, 32 * 1024, 256 * 1024, 1, false);
}

TEST_P(SslReadBufferLimitTest, WritesSmallerThanBufferLimit) { singleWriteTest(5 * 1024, 1024); }

TEST_P(SslReadBufferLimitTest, WritesLargerThanBufferLimit) { singleWriteTest(1024, 5 * 1024); }

TEST_P(SslReadBufferLimitTest, TestBind) {
  std::string address_string = TestUtility::getIpv4Loopback();
  if (version_ == Network::Address::IpVersion::v4) {
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance(address_string, 0, nullptr)};
  } else {
    address_string = "::1";
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance(address_string, 0, nullptr)};
  }

  initialize();

  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory_->createDownstreamTransportSocket(),
            stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);
        EXPECT_EQ("", server_connection_->nextProtocol());
      }));
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(address_string,
            server_connection_->connectionInfoProvider().remoteAddress()->ip()->addressAsString());

  disconnect();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/6617
TEST_P(SslReadBufferLimitTest, SmallReadsIntoSameSlice) {
  // write_size * num_writes must be large enough to cause buffer reserving fragmentation,
  // but smaller than one reservation so the expected slice to be 1.
  const uint32_t write_size = 1;
  const uint32_t num_writes = 12 * 1024;
  const uint32_t read_buffer_limit = write_size * num_writes;
  const uint32_t expected_chunk_size = write_size * num_writes;

  initialize();

  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory_->createDownstreamTransportSocket(),
            stream_info_);
        server_connection_->setBufferLimits(read_buffer_limit);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);
        EXPECT_EQ("", server_connection_->nextProtocol());
        EXPECT_EQ(read_buffer_limit, server_connection_->bufferLimit());
      }));
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  uint32_t filter_seen = 0;

  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_, _))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> Network::FilterStatus {
        EXPECT_GE(expected_chunk_size, data.length());
        EXPECT_EQ(1, data.getRawSlices().size());
        filter_seen += data.length();
        data.drain(data.length());
        if (filter_seen == (write_size * num_writes)) {
          server_connection_->close(Network::ConnectionCloseType::FlushWrite);
        }
        return Network::FilterStatus::StopIteration;
      }));

  EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        EXPECT_EQ((write_size * num_writes), filter_seen);
        dispatcher_->exit();
      }));

  for (uint32_t i = 0; i < num_writes; i++) {
    Buffer::OwnedImpl data(std::string(write_size, 'a'));
    client_connection_->write(data, false);
  }

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test asynchronous signing (ECDHE) using a private key provider.
TEST_P(SslSocketTest, RsaPrivateKeyProviderAsyncSignSuccess) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: false
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-RSA-AES128-GCM-SHA256
)EOF";

  TestUtilOptions successful_test_options(successful_client_ctx_yaml, server_ctx_yaml, true,
                                          version_);
  testUtil(successful_test_options.setPrivateKeyMethodExpected(true));
}

// Test asynchronous decryption (RSA).
TEST_P(SslSocketTest, RsaPrivateKeyProviderAsyncDecryptSuccess) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: decrypt
            sync_mode: false
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions successful_test_options(successful_client_ctx_yaml, server_ctx_yaml, true,
                                          version_);
  testUtil(successful_test_options.setPrivateKeyMethodExpected(true));
}

// Test synchronous signing (ECDHE).
TEST_P(SslSocketTest, RsaPrivateKeyProviderSyncSignSuccess) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-RSA-AES128-GCM-SHA256
)EOF";

  TestUtilOptions successful_test_options(successful_client_ctx_yaml, server_ctx_yaml, true,
                                          version_);
  testUtil(successful_test_options.setPrivateKeyMethodExpected(true));
}

// Test synchronous decryption (RSA).
TEST_P(SslSocketTest, RsaPrivateKeyProviderSyncDecryptSuccess) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: decrypt
            sync_mode: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions successful_test_options(successful_client_ctx_yaml, server_ctx_yaml, true,
                                          version_);
  testUtil(successful_test_options.setPrivateKeyMethodExpected(true));
}

// Test fallback for key provider.
TEST_P(SslSocketTest, RsaPrivateKeyProviderFallbackSuccess) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: decrypt
            sync_mode: true
            mode: rsa
            is_available: false
        fallback: true
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions successful_test_options(successful_client_ctx_yaml, server_ctx_yaml, true,
                                          version_);
  testUtil(successful_test_options.setPrivateKeyMethodExpected(true));
}

// Test asynchronous signing (ECDHE) failure (invalid signature).
TEST_P(SslSocketTest, RsaPrivateKeyProviderAsyncSignFailure) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: false
            crypto_error: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string failing_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-RSA-AES128-GCM-SHA256
)EOF";

  TestUtilOptions failing_test_options(failing_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(failing_test_options.setPrivateKeyMethodExpected(true).setExpectedServerStats(
      "ssl.connection_error"));
}

// Test synchronous signing (ECDHE) failure (invalid signature).
TEST_P(SslSocketTest, RsaPrivateKeyProviderSyncSignFailure) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: true
            crypto_error: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string failing_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-RSA-AES128-GCM-SHA256
)EOF";

  TestUtilOptions failing_test_options(failing_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(failing_test_options.setPrivateKeyMethodExpected(true).setExpectedServerStats(
      "ssl.connection_error"));
}

// Test the sign operation return with an error.
TEST_P(SslSocketTest, RsaPrivateKeyProviderSignFailure) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            method_error: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string failing_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-RSA-AES128-GCM-SHA256
)EOF";

  TestUtilOptions failing_test_options(failing_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(failing_test_options.setPrivateKeyMethodExpected(true).setExpectedServerStats(
      "ssl.connection_error"));
}

// Test the decrypt operation return with an error.
TEST_P(SslSocketTest, RsaPrivateKeyProviderDecryptFailure) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: decrypt
            method_error: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string failing_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions failing_test_options(failing_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(failing_test_options.setPrivateKeyMethodExpected(true).setExpectedServerStats(
      "ssl.connection_error"));
}

// Test the sign operation return with an error in complete.
TEST_P(SslSocketTest, RsaPrivateKeyProviderAsyncSignCompleteFailure) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            async_method_error: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string failing_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-RSA-AES128-GCM-SHA256
)EOF";

  TestUtilOptions failing_test_options(failing_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(failing_test_options.setPrivateKeyMethodExpected(true)
               .setExpectedServerCloseEvent(Network::ConnectionEvent::LocalClose)
               .setExpectedServerStats("ssl.connection_error"));
}

// Test the decrypt operation return with an error in complete.
TEST_P(SslSocketTest, RsaPrivateKeyProviderAsyncDecryptCompleteFailure) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: decrypt
            async_method_error: true
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  const std::string failing_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions failing_test_options(failing_client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(failing_test_options.setPrivateKeyMethodExpected(true)
               .setExpectedServerCloseEvent(Network::ConnectionEvent::LocalClose)
               .setExpectedServerStats("ssl.connection_error")
               .setExpectedTransportFailureReasonContains("system library")
               .setNotExpectedClientStats("ssl.connection_error"));
}

// Test having one cert with private key method and another with just
// private key.
TEST_P(SslSocketTest, RsaPrivateKeyProviderMultiCertSuccess) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SELFSIGNED_ECDSA_P256_CERT_256_HASH);

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: false
            mode: rsa
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setPrivateKeyMethodExpected(true));
}

// Test having two certs with private key methods. This will
// synchronously fail because the second certificate is a ECDSA one and
// the RSA method can't handle it.
TEST_P(SslSocketTest, RsaPrivateKeyProviderMultiCertFail) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SELFSIGNED_ECDSA_P256_CERT_256_HASH);

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: false
            mode: rsa
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
            expected_operation: sign
            sync_mode: false
            mode: rsa
)EOF";

  TestUtilOptions failing_test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  EXPECT_THROW_WITH_MESSAGE(testUtil(failing_test_options.setPrivateKeyMethodExpected(true)),
                            EnvoyException, "Private key is not RSA.")
}

// Test ECDSA private key method provider mode.
TEST_P(SslSocketTest, EcdsaPrivateKeyProviderSuccess) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SELFSIGNED_ECDSA_P256_CERT_256_HASH);

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
            expected_operation: sign
            mode: ecdsa
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setPrivateKeyMethodExpected(true));
}

// Test having two certs with different private key method modes. It's expected that the ECDSA
// provider mode is being used. RSA provider mode is set to fail with "async_method_error", but
// that's not happening.
TEST_P(SslSocketTest, RsaAndEcdsaPrivateKeyProviderMultiCertSuccess) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SELFSIGNED_ECDSA_P256_CERT_256_HASH);

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: false
            async_method_error: true
            mode: rsa
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
            expected_operation: sign
            mode: ecdsa
)EOF";
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setPrivateKeyMethodExpected(true));
}

// Test having two certs with different private key method modes. ECDSA provider is set to fail.
TEST_P(SslSocketTest, RsaAndEcdsaPrivateKeyProviderMultiCertFail) {
  const std::string client_ctx_yaml = absl::StrCat(R"EOF(
    common_tls_context:
      tls_params:
        tls_minimum_protocol_version: TLSv1_2
        tls_maximum_protocol_version: TLSv1_2
        cipher_suites:
        - ECDHE-ECDSA-AES128-GCM-SHA256
        - ECDHE-RSA-AES128-GCM-SHA256
      validation_context:
        verify_certificate_hash: )EOF",
                                                   TEST_SELFSIGNED_ECDSA_P256_CERT_256_HASH);

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
            expected_operation: sign
            sync_mode: false
            mode: rsa
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
            expected_operation: sign
            async_method_error: true
            mode: ecdsa
)EOF";
  TestUtilOptions failing_test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(failing_test_options.setPrivateKeyMethodExpected(true)
               .setExpectedServerCloseEvent(Network::ConnectionEvent::LocalClose)
               .setExpectedServerStats("ssl.connection_error"));
}

// Test private key provider and cert validation can work together.
TEST_P(SslSocketTest, PrivateKeyProviderWithCertValidation) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_chain.pem"
      private_key_provider:
        provider_name: test
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            private_key_file: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
            expected_operation: sign
            sync_mode: false
            mode: rsa
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setPrivateKeyMethodExpected(true)
               .setExpectedSha256Digest(TEST_NO_SAN_CERT_256_HASH)
               .setExpectedSha1Digest(TEST_NO_SAN_CERT_1_HASH)
               .setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL));
}

TEST_P(SslSocketTest, TestStaplesOcspResponseSuccess) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der"
  ocsp_staple_policy: lenient_stapling
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);

  std::string ocsp_response_path =
      "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der";
  std::string expected_response =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(ocsp_response_path));

  testUtil(test_options.enableOcspStapling()
               .setExpectedOcspResponse(expected_response)
               .setExpectedServerStats("ssl.ocsp_staple_responses"));
}

TEST_P(SslSocketTest, TestNoOcspStapleWhenNotEnabledOnClient) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der"
  ocsp_staple_policy: must_staple
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options);
}

TEST_P(SslSocketTest, TestOcspStapleOmittedOnSkipStaplingAndResponseExpired) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/unknown_ocsp_resp.der"
  ocsp_staple_policy: lenient_stapling
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedServerStats("ssl.ocsp_staple_omitted").enableOcspStapling());
}

TEST_P(SslSocketTest, TestConnectionFailsOnStapleRequiredAndOcspExpired) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/unknown_ocsp_resp.der"
  ocsp_staple_policy: must_staple
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.ocsp_staple_failed").enableOcspStapling());
}

TEST_P(SslSocketTest, TestConnectionSucceedsWhenRejectOnExpiredNoOcspResponse) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
  ocsp_staple_policy: strict_stapling
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedServerStats("ssl.ocsp_staple_omitted").enableOcspStapling());
}

TEST_P(SslSocketTest, TestConnectionFailsWhenRejectOnExpiredAndResponseExpired) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/unknown_ocsp_resp.der"
  ocsp_staple_policy: strict_stapling
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.ocsp_staple_failed").enableOcspStapling());
}

TEST_P(SslSocketTest, TestConnectionFailsWhenCertIsMustStapleAndResponseExpired) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_ocsp_resp.der"
  ocsp_staple_policy: lenient_stapling
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.ocsp_staple_failed").enableOcspStapling());
}

TEST_P(SslSocketTest, TestFilterMultipleCertsFilterByOcspPolicyFallbackOnFirst) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/ecdsa_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/ecdsa_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/ecdsa_ocsp_resp.der"
  ocsp_staple_policy: must_staple
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-ECDSA-AES128-GCM-SHA256
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  std::string ocsp_response_path =
      "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der";
  std::string expected_response =
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(ocsp_response_path));
  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.enableOcspStapling()
               .setExpectedServerStats("ssl.ocsp_staple_responses")
               .setExpectedOcspResponse(expected_response));
}

TEST_P(SslSocketTest, TestConnectionFailsOnMultipleCertificatesNonePassOcspPolicy) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - TLS_RSA_WITH_AES_128_GCM_SHA256
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_ocsp_resp.der"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/ecdsa_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/ecdsa_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/ecdsa_ocsp_resp.der"
  ocsp_staple_policy: must_staple
  )EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites:
      - ECDHE-ECDSA-AES128-GCM-SHA256
      - TLS_RSA_WITH_AES_128_GCM_SHA256
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, false, version_);
  testUtil(test_options.setExpectedServerStats("ssl.ocsp_staple_failed").enableOcspStapling());
}

TEST_P(SslSocketTest, Sni) {
  const std::string client_ctx_yaml = R"EOF(
    sni: "foo.bar.com"
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
)EOF";

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSni("foo.bar.com"));
}

TEST_P(SslSocketTest, AsyncCustomCertValidatorSucceeds) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      custom_validator_config:
        name: "envoy.tls.cert_validator.timed_cert_validator"
  sni: "example.com"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      custom_validator_config:
        name: "envoy.tls.cert_validator.timed_cert_validator"
)EOF";
  auto* cert_validator_factory = Registry::FactoryRegistry<CertValidatorFactory>::getFactory(
      "envoy.tls.cert_validator.timed_cert_validator");
  static_cast<TimedCertValidatorFactory*>(cert_validator_factory)->resetForTest();
  static_cast<TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(0));
  static_cast<TimedCertValidatorFactory*>(cert_validator_factory)
      ->setExpectedHostName("example.com");

  TestUtilOptions test_options(client_ctx_yaml, server_ctx_yaml, true, version_);
  testUtil(test_options.setExpectedSha256Digest(TEST_NO_SAN_CERT_256_HASH)
               .setExpectedSha1Digest(TEST_NO_SAN_CERT_1_HASH)
               .setExpectedSerialNumber(TEST_NO_SAN_CERT_SERIAL));
}

TEST_P(SslSocketTest, AsyncCustomCertValidatorFails) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
      custom_validator_config:
        name: "envoy.tls.cert_validator.timed_cert_validator"
)EOF";

  // This should fail, since the certificate has been revoked.
  const std::string revoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
)EOF";
  auto* cert_validator_factory = Registry::FactoryRegistry<CertValidatorFactory>::getFactory(
      "envoy.tls.cert_validator.timed_cert_validator");
  static_cast<TimedCertValidatorFactory*>(cert_validator_factory)->resetForTest();
  static_cast<TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(0));

  TestUtilOptions revoked_test_options(revoked_client_ctx_yaml, server_ctx_yaml, false, version_);
  revoked_test_options.setExpectedServerCloseEvent(Network::ConnectionEvent::LocalClose);
  testUtil(revoked_test_options.setExpectedServerStats("ssl.fail_verify_error")
               .setExpectedVerifyErrorCode(X509_V_ERR_CERT_REVOKED));
}

TEST_P(SslSocketTest, RsaKeyUsageVerificationEnforcementOff) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      server_tls_context.mutable_common_tls_context()->add_tls_certificates();
  // Bad server certificate to cause the mismatch between TLS usage and key usage.
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/bad_rsa_key_usage_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/bad_rsa_key_usage_key.pem"));

  updateFilterChain(server_tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_tls_context;

  // Disable the rsa_key_usage enforcement.
  client_tls_context.mutable_enforce_rsa_key_usage()->set_value(false);
  // Both server connection (Client->Envoy) and client connection (Envoy->Backend) are expected to
  // be successful.
  TestUtilOptionsV2 test_options(listener, client_tls_context, true, version_);
  // `was_key_usage_invalid` stats is expected to set to report the mismatched usage.
#ifndef BORINGSSL_FIPS
  test_options.setExpectedClientStats("ssl.was_key_usage_invalid");
#endif
  testUtilV2(test_options);
}

TEST_P(SslSocketTest, RsaKeyUsageVerificationEnforcementOn) {
  envoy::config::listener::v3::Listener listener;
  envoy::config::listener::v3::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext server_tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      server_tls_context.mutable_common_tls_context()->add_tls_certificates();
  // Bad server certificate to cause the mismatch between TLS usage and key usage.
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/bad_rsa_key_usage_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/bad_rsa_key_usage_key.pem"));

  updateFilterChain(server_tls_context, *filter_chain);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext client_tls_context;

  // Enable the rsa_key_usage enforcement.
  client_tls_context.mutable_enforce_rsa_key_usage()->set_value(true);
  TestUtilOptionsV2 test_options(listener, client_tls_context, /*expect_success=*/false, version_,
                                 /*skip_server_failure_reason_check=*/true);
  // Client connection is failed with key_usage_mismatch.
  test_options.setExpectedTransportFailureReasonContains("KEY_USAGE_BIT_INCORRECT");
  // Server connection error was not populated in this case.
  test_options.setExpectedServerStats("");
  testUtilV2(test_options);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
