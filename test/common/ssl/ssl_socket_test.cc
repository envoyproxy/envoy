#include <cstdint>
#include <memory>
#include <string>

#include "envoy/network/transport_socket.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_impl.h"
#include "common/ssl/ssl_socket.h"

#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/common/ssl/ssl_certs_test.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Ssl {

namespace {

void testUtil(const std::string& client_ctx_yaml, const std::string& server_ctx_yaml,
              const std::string& expected_digest, const std::string& expected_uri,
              const std::string& expected_local_uri, const std::string& expected_serial_number,
              const std::string& expected_subject, const std::string& expected_local_subject,
              const std::string& expected_peer_cert, const std::string& expected_stats,
              bool expect_success, const Network::Address::IpVersion version) {
  Runtime::MockLoader runtime;
  Event::SimulatedTimeSystem time_system;

  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;

  envoy::api::v2::auth::DownstreamTlsContext server_tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = std::make_unique<ServerContextConfigImpl>(server_tls_context, factory_context);
  ContextManagerImpl manager(runtime, time_system);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(
      std::move(server_cfg), manager, server_stats_store, std::vector<std::string>{});

  DangerousDeprecatedTestTime test_time;
  Event::DispatcherImpl dispatcher(test_time.timeSystem());
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version), nullptr,
                                  true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher.createListener(socket, callbacks, true, false);

  envoy::api::v2::auth::UpstreamTlsContext client_tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), client_tls_context);
  auto client_cfg = std::make_unique<ClientContextConfigImpl>(client_tls_context, factory_context);
  Stats::IsolatedStoreImpl client_stats_store;
  Ssl::ClientSslSocketFactory client_ssl_socket_factory(std::move(client_cfg), manager,
                                                        client_stats_store);
  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory.createTransportSocket(), nullptr);
  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher.createServerConnection(
            std::move(socket), server_ssl_socket_factory.createTransportSocket());
        callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  size_t counter = 0;
  auto stopSecondTime = [&]() {
    if (++counter == 2) {
      if (!expected_digest.empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(expected_digest, server_connection->ssl()->sha256PeerCertificateDigest());
        EXPECT_EQ(expected_digest, server_connection->ssl()->sha256PeerCertificateDigest());
      }
      EXPECT_EQ(expected_uri, server_connection->ssl()->uriSanPeerCertificate());
      if (!expected_local_uri.empty()) {
        EXPECT_EQ(expected_local_uri, server_connection->ssl()->uriSanLocalCertificate());
      }
      EXPECT_EQ(expected_serial_number, server_connection->ssl()->serialNumberPeerCertificate());
      if (!expected_subject.empty()) {
        EXPECT_EQ(expected_subject, server_connection->ssl()->subjectPeerCertificate());
      }
      if (!expected_local_subject.empty()) {
        EXPECT_EQ(expected_local_subject, server_connection->ssl()->subjectLocalCertificate());
      }
      if (!expected_peer_cert.empty()) {
        // Assert twice to ensure a cached value is returned and still valid.
        EXPECT_EQ(expected_peer_cert,
                  server_connection->ssl()->urlEncodedPemEncodedPeerCertificate());
        EXPECT_EQ(expected_peer_cert,
                  server_connection->ssl()->urlEncodedPemEncodedPeerCertificate());
      }
      server_connection->close(Network::ConnectionCloseType::NoFlush);
      client_connection->close(Network::ConnectionCloseType::NoFlush);
      dispatcher.exit();
    }
  };
  auto closeSecondTime = [&]() {
    if (++counter == 2) {
      dispatcher.exit();
    }
  };

  if (expect_success) {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { stopSecondTime(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { stopSecondTime(); }));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  } else {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { closeSecondTime(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { closeSecondTime(); }));
  }

  dispatcher.run(Event::Dispatcher::RunType::Block);

  if (!expected_stats.empty()) {
    EXPECT_EQ(1UL, server_stats_store.counter(expected_stats).value());
  }
}

const std::string testUtilV2(
    const envoy::api::v2::Listener& server_proto,
    const envoy::api::v2::auth::UpstreamTlsContext& client_ctx_proto,
    const std::string& client_session, bool expect_success,
    const std::string& expected_protocol_version, const std::string& expected_server_cert_digest,
    const std::string& expected_client_cert_uri, const std::string& expected_requested_server_name,
    const std::string& expected_alpn_protocol, const std::string& expected_server_stats,
    const std::string& expected_client_stats, const Network::Address::IpVersion version) {
  Runtime::MockLoader runtime;
  Event::SimulatedTimeSystem time_system;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  ContextManagerImpl manager(runtime, time_system);
  std::string new_session = EMPTY_STRING;

  // SNI-based selection logic isn't happening in Ssl::SslSocket anymore.
  ASSERT(server_proto.filter_chains().size() == 1);
  const auto& filter_chain = server_proto.filter_chains(0);
  std::vector<std::string> server_names(filter_chain.filter_chain_match().server_names().begin(),
                                        filter_chain.filter_chain_match().server_names().end());
  auto server_cfg =
      std::make_unique<Ssl::ServerContextConfigImpl>(filter_chain.tls_context(), factory_context);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(std::move(server_cfg), manager,
                                                        server_stats_store, server_names);

  DangerousDeprecatedTestTime test_time;
  Event::DispatcherImpl dispatcher(test_time.timeSystem());
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version), nullptr,
                                  true);
  NiceMock<Network::MockListenerCallbacks> callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher.createListener(socket, callbacks, true, false);

  auto client_cfg = std::make_unique<ClientContextConfigImpl>(client_ctx_proto, factory_context);
  Stats::IsolatedStoreImpl client_stats_store;
  ClientSslSocketFactory client_ssl_socket_factory(std::move(client_cfg), manager,
                                                   client_stats_store);
  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory.createTransportSocket(), nullptr);

  if (!client_session.empty()) {
    const Ssl::SslSocket* ssl_socket =
        dynamic_cast<const Ssl::SslSocket*>(client_connection->ssl());
    SSL* client_ssl_socket = ssl_socket->rawSslForTest();
    SSL_CTX* client_ssl_context = SSL_get_SSL_CTX(client_ssl_socket);
    SSL_SESSION* client_ssl_session =
        SSL_SESSION_from_bytes(reinterpret_cast<const uint8_t*>(client_session.data()),
                               client_session.size(), client_ssl_context);
    int rc = SSL_set_session(client_ssl_socket, client_ssl_session);
    ASSERT(rc == 1);
    SSL_SESSION_free(client_ssl_session);
  }

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        socket->setRequestedServerName(client_ctx_proto.sni());
        Network::ConnectionPtr new_connection = dispatcher.createServerConnection(
            std::move(socket), server_ssl_socket_factory.createTransportSocket());
        callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  size_t counter = 0;
  auto stopSecondTime = [&]() {
    if (++counter == 2) {
      if (!expected_server_cert_digest.empty()) {
        EXPECT_EQ(expected_server_cert_digest,
                  client_connection->ssl()->sha256PeerCertificateDigest());
      }
      if (!expected_alpn_protocol.empty()) {
        EXPECT_EQ(expected_alpn_protocol, client_connection->nextProtocol());
      }
      EXPECT_EQ(expected_client_cert_uri, server_connection->ssl()->uriSanPeerCertificate());
      const Ssl::SslSocket* ssl_socket =
          dynamic_cast<const Ssl::SslSocket*>(client_connection->ssl());
      SSL* client_ssl_socket = ssl_socket->rawSslForTest();
      if (!expected_protocol_version.empty()) {
        EXPECT_EQ(expected_protocol_version, SSL_get_version(client_ssl_socket));
      }
      SSL_SESSION* client_ssl_session = SSL_get_session(client_ssl_socket);
      EXPECT_TRUE(SSL_SESSION_is_resumable(client_ssl_session));
      uint8_t* session_data;
      size_t session_len;
      int rc = SSL_SESSION_to_bytes(client_ssl_session, &session_data, &session_len);
      ASSERT(rc == 1);
      new_session = std::string(reinterpret_cast<char*>(session_data), session_len);
      OPENSSL_free(session_data);
      server_connection->close(Network::ConnectionCloseType::NoFlush);
      client_connection->close(Network::ConnectionCloseType::NoFlush);
      dispatcher.exit();
    }
  };
  auto closeSecondTime = [&]() {
    if (++counter == 2) {
      dispatcher.exit();
    }
  };

  if (expect_success) {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { stopSecondTime(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          EXPECT_EQ(expected_requested_server_name, server_connection->requestedServerName());
          stopSecondTime();
        }));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  } else {
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { closeSecondTime(); }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { closeSecondTime(); }));
  }

  dispatcher.run(Event::Dispatcher::RunType::Block);

  if (!expected_server_stats.empty()) {
    EXPECT_EQ(1UL, server_stats_store.counter(expected_server_stats).value());
  }

  if (!expected_client_stats.empty()) {
    EXPECT_EQ(1UL, client_stats_store.counter(expected_client_stats).value());
  }

  return new_session;
}

// Configure the listener with unittest{cert,key}.pem and ca_cert.pem.
// Configure the client with expired_san_uri_{cert,key}.pem
void configureServerAndExpiredClientCertificate(envoy::api::v2::Listener& listener,
                                                envoy::api::v2::auth::UpstreamTlsContext& client) {
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_tmpdir }}/unittestcert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_tmpdir }}/unittestkey.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));

  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/expired_san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/expired_san_uri_key.pem"));
}

} // namespace

class SslSocketTest : public SslCertsTest,
                      public testing::WithParamInterface<Network::Address::IpVersion> {
protected:
  SslSocketTest() : dispatcher_(std::make_unique<Event::DispatcherImpl>(test_time_.timeSystem())) {}

  DangerousDeprecatedTestTime test_time_;
  std::unique_ptr<Event::DispatcherImpl> dispatcher_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, SslSocketTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(SslSocketTest, GetCertDigest) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml,
           "4444fbca965d916475f04fb4dd234dd556adb028ceb4300fa8ad6f2983c6aaa3", "", "",
           "f3828eb24fd779cf", "", "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, GetCertDigestInline) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();

  // From test/common/ssl/test_data/san_dns_cert.pem.
  server_cert->mutable_certificate_chain()->set_inline_bytes(
      "-----BEGIN CERTIFICATE-----\n"
      "MIIDDDCCAnWgAwIBAgIJAPOCjrJP13nQMA0GCSqGSIb3DQEBCwUAMHYxCzAJBgNV\n"
      "BAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9ybmlhMRYwFAYDVQQHEw1TYW4gRnJhbmNp\n"
      "c2NvMQ0wCwYDVQQKEwRMeWZ0MRkwFwYDVQQLExBMeWZ0IEVuZ2luZWVyaW5nMRAw\n"
      "DgYDVQQDEwdUZXN0IENBMB4XDTE3MDcwOTAxMzkzMloXDTE5MDcwOTAxMzkzMlow\n"
      "ejELMAkGA1UEBhMCVVMxEzARBgNVBAgTCkNhbGlmb3JuaWExFjAUBgNVBAcTDVNh\n"
      "biBGcmFuY2lzY28xDTALBgNVBAoTBEx5ZnQxGTAXBgNVBAsTEEx5ZnQgRW5naW5l\n"
      "ZXJpbmcxFDASBgNVBAMTC1Rlc3QgU2VydmVyMIGfMA0GCSqGSIb3DQEBAQUAA4GN\n"
      "ADCBiQKBgQDARNUJMFkWF0E6mbdz/nkydVC4TU2SgR95vhJhWpG6xKkCNoXkJxNz\n"
      "XOmFUUIXQyq7FnIWACYuMrE2KXnomeCGP9A6M21lumNseYSLX3/b+ao4E6gimm1/\n"
      "Gp8C3FaoAs8Ep7VE+o2DMIfTIPJhFf6RBFPundGhEm8/gv+QObVhKQIDAQABo4Gd\n"
      "MIGaMAwGA1UdEwEB/wQCMAAwCwYDVR0PBAQDAgXgMB0GA1UdJQQWMBQGCCsGAQUF\n"
      "BwMCBggrBgEFBQcDATAeBgNVHREEFzAVghNzZXJ2ZXIxLmV4YW1wbGUuY29tMB0G\n"
      "A1UdDgQWBBRCcUr8mIigWlR61OX/gmDY5vBV6jAfBgNVHSMEGDAWgBQ7eKRRTxaE\n"
      "kxxIKHoMrSuWQcp9eTANBgkqhkiG9w0BAQsFAAOBgQAtn05e8U41heun5L7MKflv\n"
      "tJM7w0whavdS8hLe63CxnS98Ap973mSiShKG+OxSJ0ClMWIZPy+KyC+T8yGIaynj\n"
      "wEEuoSGRWmhzcMMnZWxqQyD95Fsx6mtdnq/DJxiYzmH76fALe/538j8pTcoygSGD\n"
      "NWw1EW8TEwlFyuvCrlWQcg==\n"
      "-----END CERTIFICATE-----");

  // From test/common/ssl/test_data/san_dns_key.pem.
  server_cert->mutable_private_key()->set_inline_bytes(
      "-----BEGIN RSA PRIVATE KEY-----\n"
      "MIICXQIBAAKBgQDARNUJMFkWF0E6mbdz/nkydVC4TU2SgR95vhJhWpG6xKkCNoXk\n"
      "JxNzXOmFUUIXQyq7FnIWACYuMrE2KXnomeCGP9A6M21lumNseYSLX3/b+ao4E6gi\n"
      "mm1/Gp8C3FaoAs8Ep7VE+o2DMIfTIPJhFf6RBFPundGhEm8/gv+QObVhKQIDAQAB\n"
      "AoGBAJM64kukC0QAUMHX/gRD5HkAHuzSvUknuXuXUincmeWEPMtmBwdb6OgZSPT+\n"
      "8XYwx+L14Cz6tkIALXWFM0YrtyKfVdELRRs8dw5nenzK3wOeo/N/7XL4kwim4kV3\n"
      "q817RO6NUN76vHOsvQMFsPlEfCZpOTIGJEJBI7eFLP0djOMlAkEA/yWEPfQoER/i\n"
      "X6uNyXrU51A6gxyZg7rPNP0cxxRhDedtsJPNY6Tlu90v9SiTgQLUTp7BINH24t9a\n"
      "MST1tmax+wJBAMDpeRy52q+sqLXI1C2oHPuXrXzeyp9pynV/9tsYL9+qoyP2XcEZ\n"
      "DaI0tfXDJXOdYIaDnSfB50eqQUnaTmQjtCsCQGUFGaLd9K8zDJIMforzUzByl3gp\n"
      "7q41XK0COk6oRvUWWFu9aWi2dS84mDBc7Gn8EMtAF/9CopmZDUC//XlGl9kCQQCr\n"
      "6yWw8PywFHohzwEwUyLJIKpOnyoKGTiBsHGpXYvEk4hiEzwISzB4PutuQuRMfZM5\n"
      "LW/Pr6FSn6shivjTi3ITAkACMTBczBQ+chMBcTXDqyqwccQOIhupxani9wfZhsrm\n"
      "ZXbTTxnUZioQ2l/7IWa+K2O2NrWWT7b3KpCAob0bJsQz\n"
      "-----END RSA PRIVATE KEY-----");

  // From test/common/ssl/test_data/ca_certificates.pem.
  filter_chain->mutable_tls_context()
      ->mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_trusted_ca()
      ->set_inline_bytes("-----BEGIN CERTIFICATE-----\n"
                         "MIICzTCCAjagAwIBAgIJAPAEjHA3MX2BMA0GCSqGSIb3DQEBCwUAMHYxCzAJBgNV\n"
                         "BAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9ybmlhMRYwFAYDVQQHEw1TYW4gRnJhbmNp\n"
                         "c2NvMQ0wCwYDVQQKEwRMeWZ0MRkwFwYDVQQLExBMeWZ0IEVuZ2luZWVyaW5nMRAw\n"
                         "DgYDVQQDEwdGYWtlIENBMB4XDTE3MDcwOTAxMzkzMloXDTI3MDcwNzAxMzkzMlow\n"
                         "djELMAkGA1UEBhMCVVMxEzARBgNVBAgTCkNhbGlmb3JuaWExFjAUBgNVBAcTDVNh\n"
                         "biBGcmFuY2lzY28xDTALBgNVBAoTBEx5ZnQxGTAXBgNVBAsTEEx5ZnQgRW5naW5l\n"
                         "ZXJpbmcxEDAOBgNVBAMTB0Zha2UgQ0EwgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJ\n"
                         "AoGBAK/89R7j0kwhGh7NDAiuhlVmN1Hh73sGRzDBgON/ZfmJSgkvZIwQ4+hu0wZ6\n"
                         "wjN0am1iIKyp8O1OkmKurcfCRQM2QGiIUI7v7rSa0GzqENoEsQGXVk8/yhwD8Ys9\n"
                         "IPLTWXLVyh49yh5FCs6nt2/LrIuZX/K2cZQx+VuLCcg5sTllAgMBAAGjYzBhMA8G\n"
                         "A1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgEGMB0GA1UdDgQWBBRRsE37S/H/\n"
                         "lCd1pWH4sCn9qrON3TAfBgNVHSMEGDAWgBRRsE37S/H/lCd1pWH4sCn9qrON3TAN\n"
                         "BgkqhkiG9w0BAQsFAAOBgQAvFbQm1PrqeWlZssPDPfI5dlo2KEL9VedIlThqDU4z\n"
                         "MStNR/Tfw2A0WtjOyjm2R0viqLu8JdDe8umUqO9DkRhZOaUtLgQEkaQn75z8WQPk\n"
                         "Sj+kenx0v3GMrPZMAsELiPG5PcSk8l5prDgU7VkzNvoCtypCsB7RQRyvZs52Qih3\n"
                         "Ug==\n"
                         "-----END CERTIFICATE-----\n"
                         "-----BEGIN CERTIFICATE-----\n"
                         "MIICzTCCAjagAwIBAgIJAOrzsOodDleaMA0GCSqGSIb3DQEBCwUAMHYxCzAJBgNV\n"
                         "BAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9ybmlhMRYwFAYDVQQHEw1TYW4gRnJhbmNp\n"
                         "c2NvMQ0wCwYDVQQKEwRMeWZ0MRkwFwYDVQQLExBMeWZ0IEVuZ2luZWVyaW5nMRAw\n"
                         "DgYDVQQDEwdUZXN0IENBMB4XDTE3MDcwOTAxMzkzMloXDTI3MDcwNzAxMzkzMlow\n"
                         "djELMAkGA1UEBhMCVVMxEzARBgNVBAgTCkNhbGlmb3JuaWExFjAUBgNVBAcTDVNh\n"
                         "biBGcmFuY2lzY28xDTALBgNVBAoTBEx5ZnQxGTAXBgNVBAsTEEx5ZnQgRW5naW5l\n"
                         "ZXJpbmcxEDAOBgNVBAMTB1Rlc3QgQ0EwgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJ\n"
                         "AoGBAJuJh8N5TheTHLKOxsLSAfiIu9VDeKPsV98KRJJaYCMoaof3j9wBs65HzIat\n"
                         "AunuV4DVZZ2c/x7/v741oWadYd3yqL7XSzQaeBvhXi+wv3g17FYrdxaowG7cfmsh\n"
                         "gCp7/9TRW0bRGL6Qp6od/u62L8dprdHXxnck/+sZMupam9YrAgMBAAGjYzBhMA8G\n"
                         "A1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgEGMB0GA1UdDgQWBBQ7eKRRTxaE\n"
                         "kxxIKHoMrSuWQcp9eTAfBgNVHSMEGDAWgBQ7eKRRTxaEkxxIKHoMrSuWQcp9eTAN\n"
                         "BgkqhkiG9w0BAQsFAAOBgQCN00/2k9k8HNeJ8eYuFH10jnc+td7+OaYWpRSEKCS7\n"
                         "ux3KAu0UFt90mojEMClt4Y6uP4oXTWbRzMzAgQHldHU8Gkj8tYnv7mToX7Bh/xdc\n"
                         "19epzjCmo/4Q6+16GZZvltiFjkkHSZEVI5ggljy1QdMIPRegsKKmX9mjZSCSSXD6\n"
                         "SA==\n"
                         "-----END CERTIFICATE-----");

  envoy::api::v2::auth::UpstreamTlsContext client_ctx;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client_ctx.mutable_common_tls_context()->add_tls_certificates();

  // From test/common/ssl/test_data/san_uri_cert.pem.
  client_cert->mutable_certificate_chain()->set_inline_bytes(
      "-----BEGIN CERTIFICATE-----\n"
      "MIIDFDCCAn2gAwIBAgIJAN6Kky/8alfaMA0GCSqGSIb3DQEBCwUAMHYxCzAJBgNV\n"
      "BAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9ybmlhMRYwFAYDVQQHEw1TYW4gRnJhbmNp\n"
      "c2NvMQ0wCwYDVQQKEwRMeWZ0MRkwFwYDVQQLExBMeWZ0IEVuZ2luZWVyaW5nMRAw\n"
      "DgYDVQQDEwdUZXN0IENBMB4XDTE3MDcwOTAxMzkzMloXDTE5MDcwOTAxMzkzMlow\n"
      "ejELMAkGA1UEBhMCVVMxEzARBgNVBAgTCkNhbGlmb3JuaWExFjAUBgNVBAcTDVNh\n"
      "biBGcmFuY2lzY28xDTALBgNVBAoTBEx5ZnQxGTAXBgNVBAsTEEx5ZnQgRW5naW5l\n"
      "ZXJpbmcxFDASBgNVBAMTC1Rlc3QgU2VydmVyMIGfMA0GCSqGSIb3DQEBAQUAA4GN\n"
      "ADCBiQKBgQDQNSdVcQBlmLBHoyMtMJrYRMI4HkuCYqubvBD9gfZbt+bux5Lh2CRT\n"
      "RVB9fUTh/bQ9fzZ+XpZu2OLG/otHi20Yu5XPgXL5+OSzCdzMqujGs8Ft9ugZI2UL\n"
      "TufqMfesbs7y8cJg2y+UpQZlXlJ2iImMkqdomOsrNvzFzT3/LRncywIDAQABo4Gl\n"
      "MIGiMAwGA1UdEwEB/wQCMAAwCwYDVR0PBAQDAgXgMB0GA1UdJQQWMBQGCCsGAQUF\n"
      "BwMCBggrBgEFBQcDATAmBgNVHREEHzAdhhtzcGlmZmU6Ly9seWZ0LmNvbS90ZXN0\n"
      "LXRlYW0wHQYDVR0OBBYEFCmCEC8ABzXj8S4E++Whd37lIhg5MB8GA1UdIwQYMBaA\n"
      "FDt4pFFPFoSTHEgoegytK5ZByn15MA0GCSqGSIb3DQEBCwUAA4GBAF6p8V73RNLS\n"
      "jWcS/NUuRFWNqJ0pGc06wsb7JnVnSn12zPrpzwTQOPHZYY+KZC8TtgpnLgLobWVu\n"
      "mlDkUA5/25A1O+smL2nquUaix/I3X71U3MKo59JzrxNV3mlBSOmv9K1r/jpxZ96d\n"
      "mMXJ1d7L5pUWxa3q0ANg+mywtpGMvqHL\n"
      "-----END CERTIFICATE-----");

  // From test/common/ssl/test_data/san_uri_key.pem.
  client_cert->mutable_private_key()->set_inline_bytes(
      "-----BEGIN RSA PRIVATE KEY-----\n"
      "MIICWwIBAAKBgQDQNSdVcQBlmLBHoyMtMJrYRMI4HkuCYqubvBD9gfZbt+bux5Lh\n"
      "2CRTRVB9fUTh/bQ9fzZ+XpZu2OLG/otHi20Yu5XPgXL5+OSzCdzMqujGs8Ft9ugZ\n"
      "I2ULTufqMfesbs7y8cJg2y+UpQZlXlJ2iImMkqdomOsrNvzFzT3/LRncywIDAQAB\n"
      "AoGAUczQS00+LqwydbKuW07BRz6cX5fnaq6BZYoZ0r+AnsA9xoo6NujIPL76xJK2\n"
      "wWL/sTmNm1BmId6sGipfZhhtH5kELxddygGYotTYfL9NHxf0FA3uPGnGVIEQphwa\n"
      "orAe5NVpORUzZwYQdxohXR25+DnDb4cM7TktNb32P0hO2KECQQDqqRHdYzh/EPyz\n"
      "7LEUsBPD28ijb+sFfSce+YFYVUuJBtN6zHnnpnbkuQK7EbRGVEB2XrpFj/3AFktz\n"
      "tHymGI1NAkEA4yRCPSC5VEH9jJa6gpij7t8LPJ5gOh2EvrvzFVyN49E4hE2H1kKP\n"
      "1kKs8TKbsNFgHhOxwsmTnOV0ZHlZ0THmdwJAJyDqCbBxyz5Z5Oai4IA7y3zqh9Yx\n"
      "qkikLVYNa11NqxuoR+Gwsh/f02PGQMtC9Dc4SISjKtZHya/uBO0jm86cQQJAE10c\n"
      "9H8crY0uo1SaM9X1a8DCAXny9CFeFrCJKZIJWpmUetrtMJveDUMD4VASK8G9svK0\n"
      "3ck3d1GsWYBq4sWhQwJAKP1jLt3nwQEuSS88Yi2dMC3iPCWMljatC3u6976etDg7\n"
      "AoETSNx+UIqKSg6LsNYqs4omsDPnZ2j+4m2fvHaVJg==\n"
      "-----END RSA PRIVATE KEY-----");

  testUtilV2(listener, client_ctx, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, GetCertDigestServerCertWithIntermediateCA) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_chain3.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key3.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml,
           "4444fbca965d916475f04fb4dd234dd556adb028ceb4300fa8ad6f2983c6aaa3", "", "",
           "f3828eb24fd779cf", "", "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, GetCertDigestServerCertWithoutCommonName) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_only_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_only_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml,
           "4444fbca965d916475f04fb4dd234dd556adb028ceb4300fa8ad6f2983c6aaa3", "", "",
           "f3828eb24fd779cf", "", "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, GetUriWithUriSan) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
      verify_subject_alt_name: "spiffe://lyft.com/test-team"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "spiffe://lyft.com/test-team", "",
           "de8a932ffc6a57da", "", "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, GetNoUriWithDnsSan) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
)EOF";

  // The SAN field only has DNS, expect "" for uriSanPeerCertificate().
  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "f3828eb24fd779d0", "", "", "",
           "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, NoCert) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.no_certificate", true,
           GetParam());
}

TEST_P(SslSocketTest, GetUriWithLocalUriSan) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "spiffe://lyft.com/test-team",
           "f3828eb24fd779cf", "", "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, GetSubjectsWithBothCerts) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
  require_client_certificate: true
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "f3828eb24fd779cf",
           "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
           "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US", "",
           "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, GetPeerCert) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
  require_client_certificate: true
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "f3828eb24fd779cf",
           "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
           "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
           "-----BEGIN%20CERTIFICATE-----%0A"
           "MIIC6jCCAlOgAwIBAgIJAPOCjrJP13nPMA0GCSqGSIb3DQEBCwUAMHYxCzAJBgNV%0A"
           "BAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9ybmlhMRYwFAYDVQQHEw1TYW4gRnJhbmNp%0A"
           "c2NvMQ0wCwYDVQQKEwRMeWZ0MRkwFwYDVQQLExBMeWZ0IEVuZ2luZWVyaW5nMRAw%0A"
           "DgYDVQQDEwdUZXN0IENBMB4XDTE3MDcwOTAxMzkzMloXDTE5MDcwOTAxMzkzMlow%0A"
           "ejELMAkGA1UEBhMCVVMxEzARBgNVBAgTCkNhbGlmb3JuaWExFjAUBgNVBAcTDVNh%0A"
           "biBGcmFuY2lzY28xDTALBgNVBAoTBEx5ZnQxGTAXBgNVBAsTEEx5ZnQgRW5naW5l%0A"
           "ZXJpbmcxFDASBgNVBAMTC1Rlc3QgU2VydmVyMIGfMA0GCSqGSIb3DQEBAQUAA4GN%0A"
           "ADCBiQKBgQDf1VIHYrJdYK8EfmABALrN%2F4sD38I%2FJqWsxMHHwf7CGwktBMDDY3C2%0A"
           "DHHJ7Y2h4xa04jJ6tCMHF9qIzIRtgbhpwGMb%2FBcJVat6cGGKMfCSxqrYHyXo%2FEY7%0A"
           "g7qJOMzW4ds6L787auhLsZHU8Mf9XF9vMrPyZ0EwM8Cehxz9JW2tAQIDAQABo3ww%0A"
           "ejAMBgNVHRMBAf8EAjAAMAsGA1UdDwQEAwIF4DAdBgNVHSUEFjAUBggrBgEFBQcD%0A"
           "AgYIKwYBBQUHAwEwHQYDVR0OBBYEFAWIhh5J7hfKJcdpsJz4oM0VqXIWMB8GA1Ud%0A"
           "IwQYMBaAFDt4pFFPFoSTHEgoegytK5ZByn15MA0GCSqGSIb3DQEBCwUAA4GBAI2q%0A"
           "KKsieXM9jr8Zthls1W83YIcquaO9XLnKFRZwfQk4yU3t7erQwAroq9wXm6T6NS23%0A"
           "oHhHNYIF91JP%2BA9jcY2rJCTKibTUk21mVxrmr9qxKhAPJyhWoaAnEoVBgU9R9%2Bos%0A"
           "ARHpgiMhyCDvnWCdHY5Y64oVyiWdL9aHv5s82GrV%0A"
           "-----END%20CERTIFICATE-----%0A",
           "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, FailedClientAuthCaVerificationNoClientCert) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
  require_client_certificate: true
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.fail_verify_no_cert",
           false, GetParam());
}

TEST_P(SslSocketTest, FailedClientAuthCaVerification) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.fail_verify_error",
           false, GetParam());
}

TEST_P(SslSocketTest, FailedClientAuthSanVerificationNoClientCert) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" 
      verify_subject_alt_name: "example.com"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.fail_verify_no_cert",
           false, GetParam());
}

TEST_P(SslSocketTest, FailedClientAuthSanVerification) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
      verify_subject_alt_name: "example.com"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.fail_verify_san",
           false, GetParam());
}

// By default, expired certificates are not permitted.
TEST_P(SslSocketTest, FailedClientCertificateDefaultExpirationVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::auth::UpstreamTlsContext client;

  configureServerAndExpiredClientCertificate(listener, client);

  testUtilV2(listener, client, "", false, "", "", "spiffe://lyft.com/test-team", "", "",
             "ssl.fail_verify_error", "ssl.connection_error", GetParam());
}

// Expired certificates will not be accepted when explicitly disallowed via
// allow_expired_certificate.
TEST_P(SslSocketTest, FailedClientCertificateExpirationVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::auth::UpstreamTlsContext client;

  configureServerAndExpiredClientCertificate(listener, client);

  listener.mutable_filter_chains(0)
      ->mutable_tls_context()
      ->mutable_common_tls_context()
      ->mutable_validation_context()
      ->set_allow_expired_certificate(false);

  testUtilV2(listener, client, "", false, "", "", "spiffe://lyft.com/test-team", "", "",
             "ssl.fail_verify_error", "ssl.connection_error", GetParam());
}

// Expired certificates will be accepted when explicitly allowed via allow_expired_certificate.
TEST_P(SslSocketTest, ClientCertificateExpirationAllowedVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::auth::UpstreamTlsContext client;

  configureServerAndExpiredClientCertificate(listener, client);

  listener.mutable_filter_chains(0)
      ->mutable_tls_context()
      ->mutable_common_tls_context()
      ->mutable_validation_context()
      ->set_allow_expired_certificate(true);

  testUtilV2(listener, client, "", true, "", "", "spiffe://lyft.com/test-team", "", "",
             "ssl.handshake", "ssl.handshake", GetParam());
}

// Allow expired certificates, but add a certificate hash requirement so it still fails.
TEST_P(SslSocketTest, FailedClientCertAllowExpiredBadHashVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::auth::UpstreamTlsContext client;

  configureServerAndExpiredClientCertificate(listener, client);

  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      listener.mutable_filter_chains(0)
          ->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();

  server_validation_ctx->set_allow_expired_certificate(true);
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");

  testUtilV2(listener, client, "", false, "", "", "spiffe://lyft.com/test-team", "", "",
             "ssl.fail_verify_cert_hash", "ssl.connection_error", GetParam());
}

// Allow expired certificatess, but use the wrong CA so it should fail still.
TEST_P(SslSocketTest, FailedClientCertAllowServerExpiredWrongCAVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::auth::UpstreamTlsContext client;

  configureServerAndExpiredClientCertificate(listener, client);

  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      listener.mutable_filter_chains(0)
          ->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();

  server_validation_ctx->set_allow_expired_certificate(true);

  // This fake CA was not used to sign the client's certificate.
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/fake_ca_cert.pem"));

  testUtilV2(listener, client, "", false, "", "", "spiffe://lyft.com/test-team", "", "",
             "ssl.fail_verify_error", "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, ClientCertificateHashVerification) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
      verify_certificate_hash: "DF:6F:F7:2F:E9:11:65:21:26:8F:6F:2D:D4:96:6F:51:DF:47:98:83:FE:70:37:B3:9F:75:91:6A:C3:04:9D:1A"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "spiffe://lyft.com/test-team", "",
           "de8a932ffc6a57da", "", "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, ClientCertificateHashVerificationNoCA) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      verify_certificate_hash: "DF:6F:F7:2F:E9:11:65:21:26:8F:6F:2D:D4:96:6F:51:DF:47:98:83:FE:70:37:B3:9F:75:91:6A:C3:04:9D:1A"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "spiffe://lyft.com/test-team", "",
           "de8a932ffc6a57da", "", "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, ClientCertificateHashListVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_hash(
      "df6ff72fe9116521268f6f2dd4966f51df479883fe7037b39f75916ac3049d1a");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, ClientCertificateHashListVerificationNoCA) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_hash(
      "df6ff72fe9116521268f6f2dd4966f51df479883fe7037b39f75916ac3049d1a");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationNoClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
      verify_certificate_hash: "DF:6F:F7:2F:E9:11:65:21:26:8F:6F:2D:D4:96:6F:51:DF:47:98:83:FE:70:37:B3:9F:75:91:6A:C3:04:9D:1A"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.fail_verify_no_cert",
           false, GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationNoCANoClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      verify_certificate_hash: "DF:6F:F7:2F:E9:11:65:21:26:8F:6F:2D:D4:96:6F:51:DF:47:98:83:FE:70:37:B3:9F:75:91:6A:C3:04:9D:1A"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.fail_verify_no_cert",
           false, GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationWrongClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
      verify_certificate_hash: "DF:6F:F7:2F:E9:11:65:21:26:8F:6F:2D:D4:96:6F:51:DF:47:98:83:FE:70:37:B3:9F:75:91:6A:C3:04:9D:1A"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "",
           "ssl.fail_verify_cert_hash", false, GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationNoCAWrongClientCertificate) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      verify_certificate_hash: "DF:6F:F7:2F:E9:11:65:21:26:8F:6F:2D:D4:96:6F:51:DF:47:98:83:FE:70:37:B3:9F:75:91:6A:C3:04:9D:1A"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "",
           "ssl.fail_verify_cert_hash", false, GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashVerificationWrongCA) {
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
)EOF";

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/fake_ca_cert.pem"
      verify_certificate_hash: "DF:6F:F7:2F:E9:11:65:21:26:8F:6F:2D:D4:96:6F:51:DF:47:98:83:FE:70:37:B3:9F:75:91:6A:C3:04:9D:1A"
)EOF";

  testUtil(client_ctx_yaml, server_ctx_yaml, "", "", "", "", "", "", "", "ssl.fail_verify_error",
           false, GetParam());
}

TEST_P(SslSocketTest, ClientCertificateSpkiVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, ClientCertificateSpkiVerificationNoCA) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationNoClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationNoCANoClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationWrongClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"));

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationNoCAWrongClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"));

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateSpkiVerificationWrongCA) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/fake_ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_error",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_error",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, ClientCertificateHashAndSpkiVerification) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, ClientCertificateHashAndSpkiVerificationNoCA) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());

  // Works even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "",
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e",
             "spiffe://lyft.com/test-team", "", "", "ssl.handshake", "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationNoClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationNoCANoClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_no_cert",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationWrongClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"));

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationNoCAWrongClientCertificate) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"));

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_cert_hash",
             "ssl.connection_error", GetParam());
}

TEST_P(SslSocketTest, FailedClientCertificateHashAndSpkiVerificationWrongCA) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CertificateValidationContext* server_validation_ctx =
      filter_chain->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/fake_ca_cert.pem"));
  server_validation_ctx->add_verify_certificate_hash(
      "0000000000000000000000000000000000000000000000000000000000000000");
  server_validation_ctx->add_verify_certificate_spki(
      "NvqYIYSbgK2vCJpQhObf77vv+bQWtc5ek5RIOwPiC9A=");

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsCertificate* client_cert =
      client.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));

  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_error",
             "ssl.connection_error", GetParam());

  // Fails even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.fail_verify_error",
             "ssl.connection_error", GetParam());
}

// Make sure that we do not flush code and do an immediate close if we have not completed the
// handshake.
TEST_P(SslSocketTest, FlushCloseDuringHandshake) {
  Runtime::MockLoader runtime;

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_certificates.pem"
)EOF";

  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), tls_context);
  auto server_cfg = std::make_unique<ServerContextConfigImpl>(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(runtime, time_system);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(
      std::move(server_cfg), manager, server_stats_store, std::vector<std::string>{});

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                                  true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher_->createListener(socket, callbacks, true, false);

  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory.createTransportSocket());
        callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
        Buffer::OwnedImpl data("hello");
        server_connection->write(data, false);
        server_connection->close(Network::ConnectionCloseType::FlushWrite);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that half-close is sent and received correctly
TEST_P(SslSocketTest, HalfClose) {
  Runtime::MockLoader runtime;

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_certificates.pem"
)EOF";

  envoy::api::v2::auth::DownstreamTlsContext server_tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = std::make_unique<ServerContextConfigImpl>(server_tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(runtime, time_system);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(
      std::move(server_cfg), manager, server_stats_store, std::vector<std::string>{});

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                                  true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher_->createListener(socket, listener_callbacks, true, false);
  std::shared_ptr<Network::MockReadFilter> server_read_filter(new Network::MockReadFilter());
  std::shared_ptr<Network::MockReadFilter> client_read_filter(new Network::MockReadFilter());

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = std::make_unique<ClientContextConfigImpl>(tls_context, factory_context_);
  Stats::IsolatedStoreImpl client_stats_store;
  ClientSslSocketFactory client_ssl_socket_factory(std::move(client_cfg), manager,
                                                   client_stats_store);
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      client_ssl_socket_factory.createTransportSocket(), nullptr);
  client_connection->enableHalfClose(true);
  client_connection->addReadFilter(client_read_filter);
  client_connection->connect();
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(listener_callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory.createTransportSocket());
        listener_callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->enableHalfClose(true);
        server_connection->addReadFilter(server_read_filter);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
        Buffer::OwnedImpl data("hello");
        server_connection->write(data, true);
      }));

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

TEST_P(SslSocketTest, ClientAuthMultipleCAs) {
  Runtime::MockLoader runtime;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_certificates.pem"
)EOF";

  envoy::api::v2::auth::DownstreamTlsContext server_tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), server_tls_context);
  auto server_cfg = std::make_unique<ServerContextConfigImpl>(server_tls_context, factory_context);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(runtime, time_system);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(
      std::move(server_cfg), manager, server_stats_store, std::vector<std::string>{});

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                                  true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher_->createListener(socket, callbacks, true, false);

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);
  auto client_cfg = std::make_unique<ClientContextConfigImpl>(tls_context, factory_context);
  Stats::IsolatedStoreImpl client_stats_store;
  ClientSslSocketFactory ssl_socket_factory(std::move(client_cfg), manager, client_stats_store);
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory.createTransportSocket(), nullptr);

  // Verify that server sent list with 2 acceptable client certificate CA names.
  const Ssl::SslSocket* ssl_socket = dynamic_cast<const Ssl::SslSocket*>(client_connection->ssl());
  SSL_set_cert_cb(ssl_socket->rawSslForTest(),
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
  EXPECT_CALL(callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory.createTransportSocket());
        callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

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
                                 const Network::Address::IpVersion ip_version) {
  Runtime::MockLoader runtime;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(runtime, time_system);

  envoy::api::v2::auth::DownstreamTlsContext server_tls_context1;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml1), server_tls_context1);
  auto server_cfg1 =
      std::make_unique<ServerContextConfigImpl>(server_tls_context1, factory_context);

  envoy::api::v2::auth::DownstreamTlsContext server_tls_context2;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml2), server_tls_context2);
  auto server_cfg2 =
      std::make_unique<ServerContextConfigImpl>(server_tls_context2, factory_context);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory1(std::move(server_cfg1), manager,
                                                         server_stats_store, server_names1);
  Ssl::ServerSslSocketFactory server_ssl_socket_factory2(std::move(server_cfg2), manager,
                                                         server_stats_store, server_names2);

  Network::TcpListenSocket socket1(Network::Test::getCanonicalLoopbackAddress(ip_version), nullptr,
                                   true);
  Network::TcpListenSocket socket2(Network::Test::getCanonicalLoopbackAddress(ip_version), nullptr,
                                   true);
  NiceMock<Network::MockListenerCallbacks> callbacks;
  Network::MockConnectionHandler connection_handler;
  DangerousDeprecatedTestTime test_time;
  Event::DispatcherImpl dispatcher(test_time.timeSystem());
  Network::ListenerPtr listener1 = dispatcher.createListener(socket1, callbacks, true, false);
  Network::ListenerPtr listener2 = dispatcher.createListener(socket2, callbacks, true, false);

  envoy::api::v2::auth::UpstreamTlsContext client_tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), client_tls_context);
  auto client_cfg = std::make_unique<ClientContextConfigImpl>(client_tls_context, factory_context);
  Stats::IsolatedStoreImpl client_stats_store;
  ClientSslSocketFactory ssl_socket_factory(std::move(client_cfg), manager, client_stats_store);
  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket1.localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory.createTransportSocket(), nullptr);

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  SSL_SESSION* ssl_session = nullptr;
  Network::ConnectionPtr server_connection;
  EXPECT_CALL(callbacks, onAccept_(_, _))
      .WillRepeatedly(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::TransportSocketFactory& tsf = socket->localAddress() == socket1.localAddress()
                                                   ? server_ssl_socket_factory1
                                                   : server_ssl_socket_factory2;
        Network::ConnectionPtr new_connection =
            dispatcher.createServerConnection(std::move(socket), tsf.createTransportSocket());
        callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke(
          [&](Network::ConnectionPtr& conn) -> void { server_connection = std::move(conn); }));

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        const Ssl::SslSocket* ssl_socket =
            dynamic_cast<const Ssl::SslSocket*>(client_connection->ssl());
        ssl_session = SSL_get1_session(ssl_socket->rawSslForTest());
        EXPECT_TRUE(SSL_SESSION_is_resumable(ssl_session));
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(0UL, client_stats_store.counter("ssl.session_reused").value());

  client_connection = dispatcher.createClientConnection(
      socket2.localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory.createTransportSocket(), nullptr);
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  const Ssl::SslSocket* ssl_socket = dynamic_cast<const Ssl::SslSocket*>(client_connection->ssl());
  SSL_set_session(ssl_socket->rawSslForTest(), ssl_session);
  SSL_SESSION_free(ssl_session);

  client_connection->connect();

  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  // Different tests have different order of whether client or server gets Connected event
  // first, so always wait until both have happened.
  unsigned connect_count = 0;
  auto stopSecondTime = [&]() {
    connect_count++;
    if (connect_count == 2) {
      client_connection->close(Network::ConnectionCloseType::NoFlush);
      server_connection->close(Network::ConnectionCloseType::NoFlush);
      dispatcher.exit();
    }
  };

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { stopSecondTime(); }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { stopSecondTime(); }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(expect_reuse ? 1UL : 0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(expect_reuse ? 1UL : 0UL, client_stats_store.counter("ssl.session_reused").value());
}
} // namespace

TEST_P(SslSocketTest, TicketSessionResumption) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml, {}, server_ctx_yaml, {}, client_ctx_yaml, true,
                              GetParam());
}

TEST_P(SslSocketTest, TicketSessionResumptionWithClientCA) {
  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  testTicketSessionResumption(server_ctx_yaml, {}, server_ctx_yaml, {}, client_ctx_yaml, true,
                              GetParam());
}

TEST_P(SslSocketTest, TicketSessionResumptionRotateKey) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_b"
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, true,
                              GetParam());
}

TEST_P(SslSocketTest, TicketSessionResumptionWrongKey) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_b"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              GetParam());
}

// Sessions cannot be resumed even though the server certificates are the same,
// because of the different SNI requirements.
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerNames) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  std::vector<std::string> server_names1 = {"server1.example.com"};

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, server_names1, server_ctx_yaml2, {},
                              client_ctx_yaml, false, GetParam());
}

// Sessions can be resumed because the server certificates are different but the CN/SANs and
// issuer are identical
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerCert) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert2.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key2.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, true,
                              GetParam());
}

// Sessions cannot be resumed because the server certificates are different, CN/SANs are identical,
// but the issuer is different.
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerCertIntermediateCA) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_chain3.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key3.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              GetParam());
}

// Sessions cannot be resumed because the server certificates are different and the SANs
// are not identical
TEST_P(SslSocketTest, TicketSessionResumptionDifferentServerCertDifferentSAN) {
  const std::string server_ctx_yaml1 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string server_ctx_yaml2 = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  const std::string client_ctx_yaml = R"EOF(
    common_tls_context:
  )EOF";

  testTicketSessionResumption(server_ctx_yaml1, {}, server_ctx_yaml2, {}, client_ctx_yaml, false,
                              GetParam());
}

// Test that if two listeners use the same cert and session ticket key, but
// different client CA, that sessions cannot be resumed.
TEST_P(SslSocketTest, ClientAuthCrossListenerSessionResumption) {
  Runtime::MockLoader runtime;

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  require_client_certificate: true
)EOF";

  const std::string server2_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/fake_ca_cert.pem"
  require_client_certificate: true
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
)EOF";

  envoy::api::v2::auth::DownstreamTlsContext tls_context1;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), tls_context1);
  auto server_cfg = std::make_unique<ServerContextConfigImpl>(tls_context1, factory_context_);
  envoy::api::v2::auth::DownstreamTlsContext tls_context2;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server2_ctx_yaml), tls_context2);
  auto server2_cfg = std::make_unique<ServerContextConfigImpl>(tls_context2, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(runtime, time_system);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(
      std::move(server_cfg), manager, server_stats_store, std::vector<std::string>{});
  Ssl::ServerSslSocketFactory server2_ssl_socket_factory(
      std::move(server2_cfg), manager, server_stats_store, std::vector<std::string>{});

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                                  true);
  Network::TcpListenSocket socket2(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                                   true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher_->createListener(socket, callbacks, true, false);
  Network::ListenerPtr listener2 = dispatcher_->createListener(socket2, callbacks, true, false);
  const std::string client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml), tls_context);

  auto client_cfg = std::make_unique<ClientContextConfigImpl>(tls_context, factory_context_);
  Stats::IsolatedStoreImpl client_stats_store;
  ClientSslSocketFactory ssl_socket_factory(std::move(client_cfg), manager, client_stats_store);
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory.createTransportSocket(), nullptr);

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  SSL_SESSION* ssl_session = nullptr;
  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_, _))
      .WillRepeatedly(Invoke([&](Network::ConnectionSocketPtr& accepted_socket, bool) -> void {
        Network::TransportSocketFactory& tsf =
            accepted_socket->localAddress() == socket.localAddress() ? server_ssl_socket_factory
                                                                     : server2_ssl_socket_factory;

        Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
            std::move(accepted_socket), tsf.createTransportSocket());
        callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        const Ssl::SslSocket* ssl_socket =
            dynamic_cast<const Ssl::SslSocket*>(client_connection->ssl());
        ssl_session = SSL_get1_session(ssl_socket->rawSslForTest());
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
      socket2.localAddress(), Network::Address::InstanceConstSharedPtr(),
      ssl_socket_factory.createTransportSocket(), nullptr);
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  const Ssl::SslSocket* ssl_socket = dynamic_cast<const Ssl::SslSocket*>(client_connection->ssl());
  SSL_set_session(ssl_socket->rawSslForTest(), ssl_session);
  SSL_SESSION_free(ssl_session);

  client_connection->connect();

  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, client_stats_store.counter("ssl.connection_error").value());
  EXPECT_EQ(0UL, server_stats_store.counter("ssl.session_reused").value());
  EXPECT_EQ(0UL, client_stats_store.counter("ssl.session_reused").value());
}

TEST_P(SslSocketTest, SslError) {
  Runtime::MockLoader runtime;

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/fake_ca_cert.pem"
      verify_certificate_hash: "7B:0C:3F:0D:97:0E:FC:16:70:11:7A:0C:35:75:54:6B:17:AB:CF:20:D8:AA:A0:ED:87:08:0F:FB:60:4C:40:77"
)EOF";

  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml), tls_context);
  auto server_cfg = std::make_unique<ServerContextConfigImpl>(tls_context, factory_context_);
  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(runtime, time_system);
  Stats::IsolatedStoreImpl server_stats_store;
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(
      std::move(server_cfg), manager, server_stats_store, std::vector<std::string>{});

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                                  true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher_->createListener(socket, callbacks, true, false);

  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();
  Buffer::OwnedImpl bad_data("bad_handshake_data");
  client_connection->write(bad_data, false);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory.createTransportSocket());
        callbacks.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, server_stats_store.counter("ssl.connection_error").value());
}

TEST_P(SslSocketTest, ProtocolVersions) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::TlsParameters* server_params =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->mutable_tls_params();

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsParameters* client_params =
      client.mutable_common_tls_context()->mutable_tls_params();

  // Connection using defaults (client & server) succeeds, negotiating TLSv1.2.
  testUtilV2(listener, client, "", true, "TLSv1.2", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());

  // Connection using defaults (client & server) succeeds, negotiating TLSv1.2,
  // even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "TLSv1.2", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());
  client.set_allow_renegotiation(false);

  // Connection using TLSv1.0 (client) and defaults (server) succeeds.
  client_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_0);
  client_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_0);
  testUtilV2(listener, client, "", true, "TLSv1", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());

  // Connection using TLSv1.1 (client) and defaults (server) succeeds.
  client_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_1);
  client_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_1);
  testUtilV2(listener, client, "", true, "TLSv1.1", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());

  // Connection using TLSv1.2 (client) and defaults (server) succeeds.
  client_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_2);
  client_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_2);
  testUtilV2(listener, client, "", true, "TLSv1.2", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());

  // Connection using TLSv1.3 (client) and defaults (server) fails.
  client_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_3);
  client_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_3);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.connection_error",
             "ssl.connection_error", GetParam());

  // Connection using TLSv1.3 (client) and TLSv1.0-1.3 (server) succeeds.
  server_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_0);
  server_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_3);
  testUtilV2(listener, client, "", true, "TLSv1.3", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());

  // Connection using defaults (client) and TLSv1.0 (server) succeeds.
  client_params->clear_tls_minimum_protocol_version();
  client_params->clear_tls_maximum_protocol_version();
  server_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_0);
  server_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_0);
  testUtilV2(listener, client, "", true, "TLSv1", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());

  // Connection using defaults (client) and TLSv1.1 (server) succeeds.
  server_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_1);
  server_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_1);
  testUtilV2(listener, client, "", true, "TLSv1.1", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());

  // Connection using defaults (client) and TLSv1.2 (server) succeeds.
  server_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_2);
  server_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_2);
  testUtilV2(listener, client, "", true, "TLSv1.2", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());

  // Connection using defaults (client) and TLSv1.3 (server) fails.
  server_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_3);
  server_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_3);
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.connection_error",
             "ssl.connection_error", GetParam());

  // Connection using TLSv1.0-TLSv1.3 (client) and TLSv1.3 (server) succeeds.
  client_params->set_tls_minimum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_0);
  client_params->set_tls_maximum_protocol_version(envoy::api::v2::auth::TlsParameters::TLSv1_3);
  testUtilV2(listener, client, "", true, "TLSv1.3", "", "", "", "", "ssl.handshake",
             "ssl.handshake", GetParam());
}

TEST_P(SslSocketTest, ALPN) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::CommonTlsContext* server_ctx =
      filter_chain->mutable_tls_context()->mutable_common_tls_context();

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::CommonTlsContext* client_ctx = client.mutable_common_tls_context();

  // Connection using defaults (client & server) succeeds, no ALPN is negotiated.
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());

  // Connection using defaults (client & server) succeeds, no ALPN is negotiated,
  // even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  client.set_allow_renegotiation(false);

  // Client connects without ALPN to a server with "test" ALPN, no ALPN is negotiated.
  server_ctx->add_alpn_protocols("test");
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  server_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server without ALPN, no ALPN is negotiated.
  client_ctx->add_alpn_protocols("test");
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  client_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server with "test" ALPN, "test" ALPN is negotiated.
  client_ctx->add_alpn_protocols("test");
  server_ctx->add_alpn_protocols("test");
  testUtilV2(listener, client, "", true, "", "", "", "", "test", "ssl.handshake", "ssl.handshake",
             GetParam());
  client_ctx->clear_alpn_protocols();
  server_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server with "test" ALPN, "test" ALPN is negotiated,
  // even with client renegotiation.
  client.set_allow_renegotiation(true);
  client_ctx->add_alpn_protocols("test");
  server_ctx->add_alpn_protocols("test");
  testUtilV2(listener, client, "", true, "", "", "", "", "test", "ssl.handshake", "ssl.handshake",
             GetParam());
  client.set_allow_renegotiation(false);
  client_ctx->clear_alpn_protocols();
  server_ctx->clear_alpn_protocols();

  // Client connects with "test" ALPN to a server with "test2" ALPN, no ALPN is negotiated.
  client_ctx->add_alpn_protocols("test");
  server_ctx->add_alpn_protocols("test2");
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  client_ctx->clear_alpn_protocols();
  server_ctx->clear_alpn_protocols();
}

TEST_P(SslSocketTest, CipherSuites) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::TlsParameters* server_params =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->mutable_tls_params();

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsParameters* client_params =
      client.mutable_common_tls_context()->mutable_tls_params();

  // Connection using defaults (client & server) succeeds.
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());

  // Connection using defaults (client & server) succeeds, even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  client.set_allow_renegotiation(false);

  // Client connects with one of the supported cipher suites, connection succeeds.
  client_params->add_cipher_suites("ECDHE-RSA-CHACHA20-POLY1305");
  server_params->add_cipher_suites("ECDHE-RSA-CHACHA20-POLY1305");
  server_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  client_params->clear_cipher_suites();
  server_params->clear_cipher_suites();

  // Client connects with unsupported cipher suite, connection fails.
  client_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  server_params->add_cipher_suites("ECDHE-RSA-CHACHA20-POLY1305");
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.connection_error",
             "ssl.connection_error", GetParam());
  client_params->clear_cipher_suites();
  server_params->clear_cipher_suites();
}

TEST_P(SslSocketTest, EcdhCurves) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::auth::TlsParameters* server_params =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->mutable_tls_params();

  envoy::api::v2::auth::UpstreamTlsContext client;
  envoy::api::v2::auth::TlsParameters* client_params =
      client.mutable_common_tls_context()->mutable_tls_params();

  // Connection using defaults (client & server) succeeds.
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());

  // Connection using defaults (client & server) succeeds, even with client renegotiation.
  client.set_allow_renegotiation(true);
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  client.set_allow_renegotiation(false);

  // Client connects with one of the supported ECDH curves, connection succeeds.
  client_params->add_ecdh_curves("X25519");
  server_params->add_ecdh_curves("X25519");
  server_params->add_ecdh_curves("P-256");
  server_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  testUtilV2(listener, client, "", true, "", "", "", "", "", "ssl.handshake", "ssl.handshake",
             GetParam());
  client_params->clear_ecdh_curves();
  server_params->clear_ecdh_curves();
  server_params->clear_cipher_suites();

  // Client connects with unsupported ECDH curve, connection fails.
  client_params->add_ecdh_curves("X25519");
  server_params->add_ecdh_curves("P-256");
  server_params->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  testUtilV2(listener, client, "", false, "", "", "", "", "", "ssl.connection_error",
             "ssl.connection_error", GetParam());
  client_params->clear_ecdh_curves();
  server_params->clear_ecdh_curves();
  server_params->clear_cipher_suites();
}

TEST_P(SslSocketTest, RevokedCertificate) {

  const std::string server_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.crl"
)EOF";

  // This should fail, since the certificate has been revoked.
  const std::string revoked_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
)EOF";

  testUtil(revoked_client_ctx_yaml, server_ctx_yaml, "", "", "", "db6c9a4af16d9091", "", "", "",
           "ssl.fail_verify_error", false, GetParam());

  // This should succeed, since the cert isn't revoked.
  const std::string successful_client_ctx_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert2.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key2.pem"
)EOF";

  testUtil(successful_client_ctx_yaml, server_ctx_yaml, "", "", "", "db6c9a4af16d9091", "", "", "",
           "ssl.handshake", true, GetParam());
}

TEST_P(SslSocketTest, GetRequestedServerName) {
  envoy::api::v2::Listener listener;
  envoy::api::v2::listener::FilterChain* filter_chain = listener.add_filter_chains();
  envoy::api::v2::auth::TlsCertificate* server_cert =
      filter_chain->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));

  envoy::api::v2::auth::UpstreamTlsContext client;
  client.set_sni("lyft.com");

  testUtilV2(listener, client, "", true, "", "", "", "lyft.com", "", "ssl.handshake",
             "ssl.handshake", GetParam());
}

// Validate that if downstream secrets are not yet downloaded from SDS server, Envoy creates
// NotReadySslSocket object to handle downstream connection.
TEST_P(SslSocketTest, DownstreamNotReadySslSocket) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  EXPECT_CALL(factory_context, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context, dispatcher()).WillOnce(ReturnRef(dispatcher));
  EXPECT_CALL(factory_context, random()).WillOnce(ReturnRef(random));
  EXPECT_CALL(factory_context, stats()).WillOnce(ReturnRef(stats_store));
  EXPECT_CALL(factory_context, clusterManager()).WillOnce(ReturnRef(cluster_manager));
  EXPECT_CALL(factory_context, initManager()).WillRepeatedly(Return(&init_manager));

  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto server_cfg = std::make_unique<ServerContextConfigImpl>(tls_context, factory_context);
  EXPECT_EQ(nullptr, server_cfg->tlsCertificate());
  EXPECT_FALSE(server_cfg->isReady());

  Event::SimulatedTimeSystem time_system;
  ContextManagerImpl manager(runtime, time_system);
  Ssl::ServerSslSocketFactory server_ssl_socket_factory(std::move(server_cfg), manager, stats_store,
                                                        std::vector<std::string>{});
  auto transport_socket = server_ssl_socket_factory.createTransportSocket();
  EXPECT_EQ(EMPTY_STRING, transport_socket->protocol());
  EXPECT_EQ(nullptr, transport_socket->ssl());
  Buffer::OwnedImpl buffer;
  Network::IoResult result = transport_socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  result = transport_socket->doWrite(buffer, true);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
}

// Validate that if upstream secrets are not yet downloaded from SDS server, Envoy creates
// NotReadySslSocket object to handle upstream connection.
TEST_P(SslSocketTest, UpstreamNotReadySslSocket) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  Event::SimulatedTimeSystem time_system;
  EXPECT_CALL(factory_context, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context, dispatcher()).WillOnce(ReturnRef(dispatcher));
  EXPECT_CALL(factory_context, random()).WillOnce(ReturnRef(random));
  EXPECT_CALL(factory_context, stats()).WillOnce(ReturnRef(stats_store));
  EXPECT_CALL(factory_context, clusterManager()).WillOnce(ReturnRef(cluster_manager));
  EXPECT_CALL(factory_context, initManager()).WillRepeatedly(Return(&init_manager));

  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto client_cfg = std::make_unique<ClientContextConfigImpl>(tls_context, factory_context);
  EXPECT_EQ(nullptr, client_cfg->tlsCertificate());
  EXPECT_FALSE(client_cfg->isReady());

  ContextManagerImpl manager(runtime, time_system);
  Ssl::ClientSslSocketFactory client_ssl_socket_factory(std::move(client_cfg), manager,
                                                        stats_store);
  auto transport_socket = client_ssl_socket_factory.createTransportSocket();
  EXPECT_EQ(EMPTY_STRING, transport_socket->protocol());
  EXPECT_EQ(nullptr, transport_socket->ssl());
  Buffer::OwnedImpl buffer;
  Network::IoResult result = transport_socket->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  result = transport_socket->doWrite(buffer, true);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
}

class SslReadBufferLimitTest : public SslSocketTest {
public:
  void initialize() {
    MessageUtil::loadFromYaml(TestEnvironment::substitute(server_ctx_yaml_),
                              downstream_tls_context_);
    auto server_cfg =
        std::make_unique<ServerContextConfigImpl>(downstream_tls_context_, factory_context_);
    Event::SimulatedTimeSystem time_system;
    manager_.reset(new ContextManagerImpl(runtime_, time_system));
    server_ssl_socket_factory_.reset(new ServerSslSocketFactory(
        std::move(server_cfg), *manager_, server_stats_store_, std::vector<std::string>{}));

    listener_ = dispatcher_->createListener(socket_, listener_callbacks_, true, false);

    MessageUtil::loadFromYaml(TestEnvironment::substitute(client_ctx_yaml_), upstream_tls_context_);
    auto client_cfg =
        std::make_unique<ClientContextConfigImpl>(upstream_tls_context_, factory_context_);

    client_ssl_socket_factory_.reset(
        new ClientSslSocketFactory(std::move(client_cfg), *manager_, client_stats_store_));
    client_connection_ = dispatcher_->createClientConnection(
        socket_.localAddress(), source_address_,
        client_ssl_socket_factory_->createTransportSocket(), nullptr);
    client_connection_->addConnectionCallbacks(client_callbacks_);
    client_connection_->connect();
    read_filter_.reset(new Network::MockReadFilter());
  }

  void readBufferLimitTest(uint32_t read_buffer_limit, uint32_t expected_chunk_size,
                           uint32_t write_size, uint32_t num_writes, bool reserve_write_space) {
    initialize();

    EXPECT_CALL(listener_callbacks_, onAccept_(_, _))
        .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
          Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
              std::move(socket), server_ssl_socket_factory_->createTransportSocket());
          new_connection->setBufferLimits(read_buffer_limit);
          listener_callbacks_.onNewConnection(std::move(new_connection));
        }));
    EXPECT_CALL(listener_callbacks_, onNewConnection_(_))
        .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
          server_connection_ = std::move(conn);
          server_connection_->addConnectionCallbacks(server_callbacks_);
          server_connection_->addReadFilter(read_filter_);
          EXPECT_EQ("", server_connection_->nextProtocol());
          EXPECT_EQ(read_buffer_limit, server_connection_->bufferLimit());
        }));

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

      // Incredibly contrived way of making sure that the write buffer has an empty chain in it.
      if (reserve_write_space) {
        Buffer::RawSlice iovecs[2];
        EXPECT_EQ(2UL, data.reserve(16384, iovecs, 2));
        iovecs[0].len_ = 0;
        iovecs[1].len_ = 0;
        data.commit(iovecs, 2);
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
    dispatcher_.reset(
        new Event::DispatcherImpl(test_time_.timeSystem(), Buffer::WatermarkFactoryPtr{factory}));

    // By default, expect 4 buffers to be created - the client and server read and write buffers.
    EXPECT_CALL(*factory, create_(_, _))
        .Times(2)
        .WillOnce(Invoke([&](std::function<void()> below_low,
                             std::function<void()> above_high) -> Buffer::Instance* {
          client_write_buffer = new MockWatermarkBuffer(below_low, above_high);
          return client_write_buffer;
        }))
        .WillRepeatedly(Invoke([](std::function<void()> below_low,
                                  std::function<void()> above_high) -> Buffer::Instance* {
          return new Buffer::WatermarkBuffer(below_low, above_high);
        }));

    initialize();

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    EXPECT_CALL(listener_callbacks_, onAccept_(_, _))
        .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
          Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
              std::move(socket), server_ssl_socket_factory_->createTransportSocket());
          new_connection->setBufferLimits(read_buffer_limit);
          listener_callbacks_.onNewConnection(std::move(new_connection));
        }));
    EXPECT_CALL(listener_callbacks_, onNewConnection_(_))
        .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
          server_connection_ = std::move(conn);
          server_connection_->addConnectionCallbacks(server_callbacks_);
          server_connection_->addReadFilter(read_filter_);
          EXPECT_EQ("", server_connection_->nextProtocol());
          EXPECT_EQ(read_buffer_limit, server_connection_->bufferLimit());
        }));

    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _)).Times(testing::AnyNumber());

    std::string data_to_write(bytes_to_write, 'a');
    Buffer::OwnedImpl buffer_to_write(data_to_write);
    std::string data_written;
    EXPECT_CALL(*client_write_buffer, move(_))
        .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                              Invoke(client_write_buffer, &MockWatermarkBuffer::baseMove)));
    EXPECT_CALL(*client_write_buffer, drain(_)).WillOnce(Invoke([&](uint64_t n) -> void {
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

  Stats::IsolatedStoreImpl server_stats_store_;
  Stats::IsolatedStoreImpl client_stats_store_;
  Network::TcpListenSocket socket_{Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                                   true};
  Network::MockListenerCallbacks listener_callbacks_;
  Network::MockConnectionHandler connection_handler_;
  const std::string server_ctx_yaml_ = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_tmpdir }}/unittestcert.pem"
      private_key:
        filename: "{{ test_tmpdir }}/unittestkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
)EOF";

  const std::string client_ctx_yaml_ = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
)EOF";

  Runtime::MockLoader runtime_;
  envoy::api::v2::auth::DownstreamTlsContext downstream_tls_context_;
  std::unique_ptr<ContextManagerImpl> manager_;
  Network::TransportSocketFactoryPtr server_ssl_socket_factory_;
  Network::ListenerPtr listener_;
  envoy::api::v2::auth::UpstreamTlsContext upstream_tls_context_;
  ClientContextSharedPtr client_ctx_;
  Network::TransportSocketFactoryPtr client_ssl_socket_factory_;
  Network::ClientConnectionPtr client_connection_;
  Network::ConnectionPtr server_connection_;
  NiceMock<Network::MockConnectionCallbacks> server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  StrictMock<Network::MockConnectionCallbacks> client_callbacks_;
  Network::Address::InstanceConstSharedPtr source_address_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, SslReadBufferLimitTest,
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
  if (GetParam() == Network::Address::IpVersion::v4) {
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance(address_string, 0)};
  } else {
    address_string = "::1";
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance(address_string, 0)};
  }

  initialize();

  EXPECT_CALL(listener_callbacks_, onAccept_(_, _))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket, bool) -> void {
        Network::ConnectionPtr new_connection = dispatcher_->createServerConnection(
            std::move(socket), server_ssl_socket_factory_->createTransportSocket());
        new_connection->setBufferLimits(0);
        listener_callbacks_.onNewConnection(std::move(new_connection));
      }));
  EXPECT_CALL(listener_callbacks_, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection_ = std::move(conn);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);
        EXPECT_EQ("", server_connection_->nextProtocol());
      }));

  EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(address_string, server_connection_->remoteAddress()->ip()->addressAsString());

  disconnect();
}

} // namespace Ssl
} // namespace Envoy
