#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/ssl/connection_impl.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_impl.h"
#include "common/stats/stats_impl.h"

#include "test/common/ssl/ssl_certs_test.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::Invoke;
using testing::StrictMock;
using testing::_;

namespace Envoy {
namespace Ssl {

namespace {

void testUtil(const std::string& client_ctx_json, const std::string& server_ctx_json,
              const std::string& expected_digest, const std::string& expected_uri,
              const std::string& expected_stats, bool expect_success,
              const Network::Address::IpVersion version) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ServerContextConfigImpl server_ctx_config(*server_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(
      manager.createSslServerContext("", {}, stats_store, server_ctx_config, true));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());

  Json::ObjectSharedPtr client_ctx_loader = TestEnvironment::jsonLoadFromString(client_ctx_json);
  ClientContextConfigImpl client_ctx_config(*client_ctx_loader);
  ClientContextPtr client_ctx(manager.createSslClientContext(stats_store, client_ctx_config));
  Network::ClientConnectionPtr client_connection = dispatcher.createSslClientConnection(
      *client_ctx, socket.localAddress(), Network::Address::InstanceConstSharedPtr());
  client_connection->connect();

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  if (expect_success) {
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          if (!expected_digest.empty()) {
            EXPECT_EQ(expected_digest, server_connection->ssl()->sha256PeerCertificateDigest());
          }
          EXPECT_EQ(expected_uri, server_connection->ssl()->uriSanPeerCertificate());
          server_connection->close(Network::ConnectionCloseType::NoFlush);
          client_connection->close(Network::ConnectionCloseType::NoFlush);
          dispatcher.exit();
        }));
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  } else {
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          client_connection->close(Network::ConnectionCloseType::NoFlush);
          dispatcher.exit();
        }));
  }

  dispatcher.run(Event::Dispatcher::RunType::Block);

  if (!expected_stats.empty()) {
    EXPECT_EQ(1UL, stats_store.counter(expected_stats).value());
  }
}

const std::string testUtilV2(const envoy::api::v2::Listener& server_proto,
                             const envoy::api::v2::UpstreamTlsContext& client_ctx_proto,
                             const std::string& client_session, bool expect_success,
                             const std::string& expected_server_cert_digest,
                             const std::string& expected_client_cert_uri,
                             const std::string& expected_alpn_protocol,
                             const std::string& expected_stats, unsigned expected_stats_value,
                             const Network::Address::IpVersion version) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  std::string new_session = EMPTY_STRING;

  std::vector<ServerContextPtr> server_contexts;
  for (const auto& filter_chain : server_proto.filter_chains()) {
    if (filter_chain.has_tls_context()) {
      std::vector<std::string> sni_domains(filter_chain.filter_chain_match().sni_domains().begin(),
                                           filter_chain.filter_chain_match().sni_domains().end());
      Ssl::ServerContextConfigImpl server_ctx_config(filter_chain.tls_context());
      server_contexts.emplace_back(manager.createSslServerContext(
          "test_listener", sni_domains, stats_store, server_ctx_config, false));
    }
  }
  ASSERT(server_contexts.size() >= 1);

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version), true);
  NiceMock<Network::MockListenerCallbacks> callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher.createSslListener(
      connection_handler, *server_contexts[0], socket, callbacks, stats_store,
      Network::ListenerOptions::listenerOptionsWithBindToPort());

  ClientContextConfigImpl client_ctx_config(client_ctx_proto);
  ClientContextPtr client_ctx(manager.createSslClientContext(stats_store, client_ctx_config));
  Network::ClientConnectionPtr client_connection = dispatcher.createSslClientConnection(
      *client_ctx, socket.localAddress(), Network::Address::InstanceConstSharedPtr());

  if (!client_session.empty()) {
    Ssl::ConnectionImpl* ssl_connection =
        dynamic_cast<Ssl::ConnectionImpl*>(client_connection->ssl());
    SSL* client_ssl_connection = ssl_connection->rawSslForTest();
    SSL_CTX* client_ssl_context = SSL_get_SSL_CTX(client_ssl_connection);
    SSL_SESSION* client_ssl_session =
        SSL_SESSION_from_bytes(reinterpret_cast<const uint8_t*>(client_session.data()),
                               client_session.size(), client_ssl_context);
    int rc = SSL_set_session(client_ssl_connection, client_ssl_session);
    ASSERT(rc == 1);
    UNREFERENCED_PARAMETER(rc);
    SSL_SESSION_free(client_ssl_session);
  }

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  unsigned connect_count = 0;
  auto stopSecondTime = [&]() {
    if (++connect_count == 2) {
      if (!expected_server_cert_digest.empty()) {
        EXPECT_EQ(expected_server_cert_digest,
                  client_connection->ssl()->sha256PeerCertificateDigest());
      }
      if (!expected_alpn_protocol.empty()) {
        EXPECT_EQ(expected_alpn_protocol, client_connection->nextProtocol());
      }
      EXPECT_EQ(expected_client_cert_uri, server_connection->ssl()->uriSanPeerCertificate());
      Ssl::ConnectionImpl* ssl_connection =
          dynamic_cast<Ssl::ConnectionImpl*>(client_connection->ssl());
      SSL* client_ssl_connection = ssl_connection->rawSslForTest();
      SSL_SESSION* client_ssl_session = SSL_get_session(client_ssl_connection);
      EXPECT_FALSE(client_ssl_session->not_resumable);
      uint8_t* session_data;
      size_t session_len;
      int rc = SSL_SESSION_to_bytes(client_ssl_session, &session_data, &session_len);
      ASSERT(rc == 1);
      UNREFERENCED_PARAMETER(rc);
      new_session = std::string(reinterpret_cast<char*>(session_data), session_len);
      OPENSSL_free(session_data);
      server_connection->close(Network::ConnectionCloseType::NoFlush);
      client_connection->close(Network::ConnectionCloseType::NoFlush);
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
    EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          client_connection->close(Network::ConnectionCloseType::NoFlush);
          dispatcher.exit();
        }));
    EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  }

  dispatcher.run(Event::Dispatcher::RunType::Block);

  if (!expected_stats.empty()) {
    EXPECT_EQ(expected_stats_value, stats_store.counter(expected_stats).value());
  }

  return new_session;
}

} // namespace

class SslConnectionImplTest : public SslCertsTest,
                              public testing::WithParamInterface<Network::Address::IpVersion> {};

INSTANTIATE_TEST_CASE_P(IpVersions, SslConnectionImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(SslConnectionImplTest, GetCertDigest) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json,
           "4444fbca965d916475f04fb4dd234dd556adb028ceb4300fa8ad6f2983c6aaa3", "", "ssl.handshake",
           true, GetParam());
}

TEST_P(SslConnectionImplTest, GetCertDigestServerCertWithoutCommonName) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_only_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_only_dns_key.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json,
           "4444fbca965d916475f04fb4dd234dd556adb028ceb4300fa8ad6f2983c6aaa3", "", "ssl.handshake",
           true, GetParam());
}

TEST_P(SslConnectionImplTest, GetUriWithUriSan) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_subject_alt_name": [ "spiffe://lyft.com/test-team" ]
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "spiffe://lyft.com/test-team", "ssl.handshake",
           true, GetParam());
}

TEST_P(SslConnectionImplTest, GetNoUriWithDnsSan) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  // The SAN field only has DNS, expect "" for uriSanPeerCertificate().
  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.handshake", true, GetParam());
}

TEST_P(SslConnectionImplTest, NoCert) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "",
    "private_key_file": ""
  })EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": ""
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.no_certificate", true, GetParam());
}

TEST_P(SslConnectionImplTest, FailedClientAuthCaVerificationNoClientCert) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "",
    "private_key_file": ""
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "require_client_certificate": true
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.fail_verify_no_cert", false, GetParam());
}

TEST_P(SslConnectionImplTest, FailedClientAuthCaVerification) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.fail_verify_error", false, GetParam());
}

TEST_P(SslConnectionImplTest, FailedClientAuthSanVerificationNoClientCert) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "",
    "private_key_file": ""
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_subject_alt_name": [ "example.com" ]
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.fail_verify_no_cert", false, GetParam());
}

TEST_P(SslConnectionImplTest, FailedClientAuthSanVerification) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_subject_alt_name": [ "example.com" ]
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.fail_verify_san", false, GetParam());
}

TEST_P(SslConnectionImplTest, FailedClientAuthHashVerificationNoClientCert) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "",
    "private_key_file": ""
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_certificate_hash": "7B:0C:3F:0D:97:0E:FC:16:70:11:7A:0C:35:75:54:6B:17:AB:CF:20:D8:AA:A0:ED:87:08:0F:FB:60:4C:40:77"
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.fail_verify_no_cert", false, GetParam());
}

TEST_P(SslConnectionImplTest, FailedClientAuthHashVerification) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_certificate_hash": "7B:0C:3F:0D:97:0E:FC:16:70:11:7A:0C:35:75:54:6B:17:AB:CF:20:D8:AA:A0:ED:87:08:0F:FB:60:4C:40:77"
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", "ssl.fail_verify_cert_hash", false,
           GetParam());
}

// Make sure that we do not flush code and do an immediate close if we have not completed the
// handshake.
TEST_P(SslConnectionImplTest, FlushCloseDuringHandshake) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_certificates.pem"
  }
  )EOF";

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ServerContextConfigImpl server_ctx_config(*server_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(
      manager.createSslServerContext("", {}, stats_store, server_ctx_config, true));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr());
  client_connection->connect();
  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
        Buffer::OwnedImpl data("hello");
        server_connection->write(data);
        server_connection->close(Network::ConnectionCloseType::FlushWrite);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher.exit(); }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST_P(SslConnectionImplTest, ClientAuthMultipleCAs) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_certificates.pem"
  }
  )EOF";

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ServerContextConfigImpl server_ctx_config(*server_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(
      manager.createSslServerContext("", {}, stats_store, server_ctx_config, true));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());

  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  Json::ObjectSharedPtr client_ctx_loader = TestEnvironment::jsonLoadFromString(client_ctx_json);
  ClientContextConfigImpl client_ctx_config(*client_ctx_loader);
  ClientContextPtr client_ctx(manager.createSslClientContext(stats_store, client_ctx_config));
  Network::ClientConnectionPtr client_connection = dispatcher.createSslClientConnection(
      *client_ctx, socket.localAddress(), Network::Address::InstanceConstSharedPtr());

  // Verify that server sent list with 2 acceptable client certificate CA names.
  Ssl::ConnectionImpl* ssl_connection =
      dynamic_cast<Ssl::ConnectionImpl*>(client_connection->ssl());
  SSL_set_cert_cb(ssl_connection->rawSslForTest(),
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
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, stats_store.counter("ssl.handshake").value());
}

namespace {

// Test connecting with a client to server1, then trying to reuse the session on server2
void testTicketSessionResumption(const std::string& server_ctx_json1,
                                 const std::string& server_ctx_json2,
                                 const std::string& client_ctx_json, bool expect_reuse,
                                 const Network::Address::IpVersion ip_version) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  Json::ObjectSharedPtr server_ctx_loader1 = TestEnvironment::jsonLoadFromString(server_ctx_json1);
  Json::ObjectSharedPtr server_ctx_loader2 = TestEnvironment::jsonLoadFromString(server_ctx_json2);
  ServerContextConfigImpl server_ctx_config1(*server_ctx_loader1);
  ServerContextConfigImpl server_ctx_config2(*server_ctx_loader2);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx1(
      manager.createSslServerContext("server1", {}, stats_store, server_ctx_config1, false));
  ServerContextPtr server_ctx2(
      manager.createSslServerContext("server2", {}, stats_store, server_ctx_config2, false));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket1(Network::Test::getCanonicalLoopbackAddress(ip_version), true);
  Network::TcpListenSocket socket2(Network::Test::getCanonicalLoopbackAddress(ip_version), true);
  NiceMock<Network::MockListenerCallbacks> callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener1 = dispatcher.createSslListener(
      connection_handler, *server_ctx1, socket1, callbacks, stats_store,
      Network::ListenerOptions::listenerOptionsWithBindToPort());
  Network::ListenerPtr listener2 = dispatcher.createSslListener(
      connection_handler, *server_ctx2, socket2, callbacks, stats_store,
      Network::ListenerOptions::listenerOptionsWithBindToPort());

  Json::ObjectSharedPtr client_ctx_loader = TestEnvironment::jsonLoadFromString(client_ctx_json);
  ClientContextConfigImpl client_ctx_config(*client_ctx_loader);
  ClientContextPtr client_ctx(manager.createSslClientContext(stats_store, client_ctx_config));
  Network::ClientConnectionPtr client_connection = dispatcher.createSslClientConnection(
      *client_ctx, socket1.localAddress(), Network::Address::InstanceConstSharedPtr());

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  SSL_SESSION* ssl_session = nullptr;
  Network::ConnectionPtr server_connection;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke(
          [&](Network::ConnectionPtr& conn) -> void { server_connection = std::move(conn); }));

  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        Ssl::ConnectionImpl* ssl_connection =
            dynamic_cast<Ssl::ConnectionImpl*>(client_connection->ssl());
        ssl_session = SSL_get1_session(ssl_connection->rawSslForTest());
        EXPECT_FALSE(ssl_session->not_resumable);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(0UL, stats_store.counter("ssl.session_reused").value());

  client_connection = dispatcher.createSslClientConnection(
      *client_ctx, socket2.localAddress(), Network::Address::InstanceConstSharedPtr());
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  Ssl::ConnectionImpl* ssl_connection =
      dynamic_cast<Ssl::ConnectionImpl*>(client_connection->ssl());
  SSL_set_session(ssl_connection->rawSslForTest(), ssl_session);
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

  // One for client, one for server
  EXPECT_EQ(expect_reuse ? 2UL : 0UL, stats_store.counter("ssl.session_reused").value());
}
} // namespace

TEST_P(SslConnectionImplTest, TicketSessionResumption) {
  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": ["{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"]
  }
  )EOF";

  std::string client_ctx_json = R"EOF(
  {
  }
  )EOF";

  testTicketSessionResumption(server_ctx_json, server_ctx_json, client_ctx_json, true, GetParam());
}

TEST_P(SslConnectionImplTest, TicketSessionResumptionWithClientCA) {
  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": ["{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"],
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  testTicketSessionResumption(server_ctx_json, server_ctx_json, client_ctx_json, true, GetParam());
}

TEST_P(SslConnectionImplTest, TicketSessionResumptionRotateKey) {
  std::string server_ctx_json1 = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
    ]
  }
  )EOF";

  std::string server_ctx_json2 = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_b",
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
    ]
  }
  )EOF";

  std::string client_ctx_json = R"EOF(
  {
  }
  )EOF";

  testTicketSessionResumption(server_ctx_json1, server_ctx_json2, client_ctx_json, true,
                              GetParam());
}

TEST_P(SslConnectionImplTest, TicketSessionResumptionWrongKey) {
  std::string server_ctx_json1 = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
    ]
  }
  )EOF";

  std::string server_ctx_json2 = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_b"
    ]
  }
  )EOF";

  std::string client_ctx_json = R"EOF(
  {
  }
  )EOF";

  testTicketSessionResumption(server_ctx_json1, server_ctx_json2, client_ctx_json, false,
                              GetParam());
}

// Sessions can be resumed because the server certificates are different but the CN/SANs and
// issuer are identical
TEST_P(SslConnectionImplTest, TicketSessionResumptionDifferentServerCert) {
  std::string server_ctx_json1 = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
    ]
  }
  )EOF";

  std::string server_ctx_json2 = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert2.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key2.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
    ]
  }
  )EOF";

  std::string client_ctx_json = R"EOF(
  {
  }
  )EOF";

  testTicketSessionResumption(server_ctx_json1, server_ctx_json2, client_ctx_json, true,
                              GetParam());
}

// Sessions cannot be resumed because the server certificates are different and the SANs
// are not identical
TEST_P(SslConnectionImplTest, TicketSessionResumptionDifferentServerCertDifferentSAN) {
  std::string server_ctx_json1 = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
    ]
  }
  )EOF";

  std::string server_ctx_json2 = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
    ]
  }
  )EOF";

  std::string client_ctx_json = R"EOF(
  {
  }
  )EOF";

  testTicketSessionResumption(server_ctx_json1, server_ctx_json2, client_ctx_json, false,
                              GetParam());
}

// Test that if two listeners use the same cert and session ticket key, but
// different client CA, that sessions cannot be resumed.
TEST_P(SslConnectionImplTest, ClientAuthCrossListenerSessionResumption) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": ["{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"],
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "require_client_certificate": true
  }
  )EOF";

  std::string server2_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": ["{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"],
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/fake_ca_cert.pem",
    "require_client_certificate": true
  }
  )EOF";

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ServerContextConfigImpl server_ctx_config(*server_ctx_loader);
  Json::ObjectSharedPtr server2_ctx_loader = TestEnvironment::jsonLoadFromString(server2_ctx_json);
  ServerContextConfigImpl server2_ctx_config(*server2_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(
      manager.createSslServerContext("server1", {}, stats_store, server_ctx_config, false));
  ServerContextPtr server2_ctx(
      manager.createSslServerContext("server2", {}, stats_store, server2_ctx_config, false));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), true);
  Network::TcpListenSocket socket2(Network::Test::getCanonicalLoopbackAddress(GetParam()), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());
  Network::ListenerPtr listener2 = dispatcher.createSslListener(
      connection_handler, *server2_ctx, socket2, callbacks, stats_store,
      Network::ListenerOptions::listenerOptionsWithBindToPort());

  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  Json::ObjectSharedPtr client_ctx_loader = TestEnvironment::jsonLoadFromString(client_ctx_json);
  ClientContextConfigImpl client_ctx_config(*client_ctx_loader);
  ClientContextPtr client_ctx(manager.createSslClientContext(stats_store, client_ctx_config));
  Network::ClientConnectionPtr client_connection = dispatcher.createSslClientConnection(
      *client_ctx, socket.localAddress(), Network::Address::InstanceConstSharedPtr());

  Network::MockConnectionCallbacks client_connection_callbacks;
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  client_connection->connect();

  SSL_SESSION* ssl_session = nullptr;
  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        Ssl::ConnectionImpl* ssl_connection =
            dynamic_cast<Ssl::ConnectionImpl*>(client_connection->ssl());
        ssl_session = SSL_get1_session(ssl_connection->rawSslForTest());
        EXPECT_FALSE(ssl_session->not_resumable);
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  // 1 for client, 1 for server
  EXPECT_EQ(2UL, stats_store.counter("ssl.handshake").value());

  client_connection = dispatcher.createSslClientConnection(
      *client_ctx, socket2.localAddress(), Network::Address::InstanceConstSharedPtr());
  client_connection->addConnectionCallbacks(client_connection_callbacks);
  Ssl::ConnectionImpl* ssl_connection =
      dynamic_cast<Ssl::ConnectionImpl*>(client_connection->ssl());
  SSL_set_session(ssl_connection->rawSslForTest(), ssl_session);
  SSL_SESSION_free(ssl_session);

  client_connection->connect();

  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(client_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher.exit(); }));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, stats_store.counter("ssl.connection_error").value());
  EXPECT_EQ(0UL, stats_store.counter("ssl.session_reused").value());
}

TEST_P(SslConnectionImplTest, SslError) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_certificate_hash": "7B:0C:3F:0D:97:0E:FC:16:70:11:7A:0C:35:75:54:6B:17:AB:CF:20:D8:AA:A0:ED:87:08:0F:FB:60:4C:40:77"
  }
  )EOF";

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ServerContextConfigImpl server_ctx_config(*server_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(
      manager.createSslServerContext("", {}, stats_store, server_ctx_config, true));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr());
  client_connection->connect();
  Buffer::OwnedImpl bad_data("bad_handshake_data");
  client_connection->write(bad_data);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, stats_store.counter("ssl.connection_error").value());
}

TEST_P(SslConnectionImplTest, SniCertificate) {
  envoy::api::v2::Listener listener;

  // san_dns_cert.pem: server1.example.com
  envoy::api::v2::FilterChain* filter_chain1 = listener.add_filter_chains();
  filter_chain1->mutable_filter_chain_match()->add_sni_domains("server1.example.com");
  envoy::api::v2::TlsCertificate* server_cert1 =
      filter_chain1->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert1->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert1->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));

  // san_multiple_dns_cert.pem: server2.example.com, *.example.com
  envoy::api::v2::FilterChain* filter_chain2 = listener.add_filter_chains();
  filter_chain2->mutable_filter_chain_match()->add_sni_domains("server2.example.com");
  filter_chain2->mutable_filter_chain_match()->add_sni_domains("*.example.com");
  envoy::api::v2::TlsCertificate* server_cert2 =
      filter_chain2->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert2->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"));
  server_cert2->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem"));

  envoy::api::v2::UpstreamTlsContext client_ctx;

  // Connection to server1.example.com succeeds, client receives san_dns_cert.pem (exact match).
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("server1.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e", "", "",
             "ssl.handshake", 2, GetParam());

  // Connection to www.example.com succeeds, client receives san_multiple_dns_cert.pem
  // (wildcard match).
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("www.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "",
             "ssl.handshake", 2, GetParam());

  // Connection without SNI fails, since there is no match.
  client_ctx.clear_sni();
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.fail_no_sni_match", 1, GetParam());

  // Connection to bar.foo.com fails, since there is no match.
  client_ctx.set_sni("bar.foo.com");
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.fail_no_sni_match", 1, GetParam());

  // no_san_cert.pem: * (no SNI restrictions)
  envoy::api::v2::FilterChain* filter_chain3 = listener.add_filter_chains();
  envoy::api::v2::TlsCertificate* server_cert3 =
      filter_chain3->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert3->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"));
  server_cert3->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"));

  // Connection without SNI succeeds, client receives no_san_cert.pem (exact match on "").
  // ssl.handshake logged by both: client & server.
  client_ctx.clear_sni();
  testUtilV2(listener, client_ctx, "", true,
             "4444fbca965d916475f04fb4dd234dd556adb028ceb4300fa8ad6f2983c6aaa3", "", "",
             "ssl.handshake", 2, GetParam());

  // Connection to bar.foo.com succeeds, client receives no_san_cert.pem (exact match on "").
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("bar.foo.com");
  testUtilV2(listener, client_ctx, "", true,
             "4444fbca965d916475f04fb4dd234dd556adb028ceb4300fa8ad6f2983c6aaa3", "", "",
             "ssl.handshake", 2, GetParam());
}

TEST_P(SslConnectionImplTest, SniSessionResumption) {
  envoy::api::v2::Listener listener;

  // san_dns_cert.pem: server1.example.com, *
  envoy::api::v2::FilterChain* filter_chain1 = listener.add_filter_chains();
  filter_chain1->mutable_filter_chain_match()->add_sni_domains("server1.example.com");
  filter_chain1->mutable_filter_chain_match()->add_sni_domains(""); // Catch-all, no SNI.
  envoy::api::v2::TlsCertificate* server_cert1 =
      filter_chain1->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert1->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert1->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  filter_chain1->mutable_tls_context()->mutable_session_ticket_keys()->add_keys()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"));

  // san_multiple_dns_cert.pem: server2.example.com, *.example.com
  envoy::api::v2::FilterChain* filter_chain2 = listener.add_filter_chains();
  filter_chain2->mutable_filter_chain_match()->add_sni_domains("server2.example.com");
  filter_chain2->mutable_filter_chain_match()->add_sni_domains("*.example.com");
  envoy::api::v2::TlsCertificate* server_cert2 =
      filter_chain2->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert2->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"));
  server_cert2->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem"));
  filter_chain2->mutable_tls_context()->mutable_session_ticket_keys()->add_keys()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"));

  // san_multiple_dns_cert.pem: protected.example.com (same certificate as #2, but different SNI)
  envoy::api::v2::FilterChain* filter_chain3 = listener.add_filter_chains();
  filter_chain3->mutable_filter_chain_match()->add_sni_domains("protected.example.com");
  envoy::api::v2::TlsCertificate* server_cert3 =
      filter_chain3->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert3->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"));
  server_cert3->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem"));
  filter_chain3->mutable_tls_context()->mutable_session_ticket_keys()->add_keys()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"));

  envoy::api::v2::UpstreamTlsContext client_ctx;

  // Connection to www.example.com succeeds, new session established.
  client_ctx.set_sni("www.example.com");
  std::string client_session =
      testUtilV2(listener, client_ctx, "", true,
                 "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "",
                 "ssl.session_reused", 0, GetParam());

  // Connection to www.example.com succeeds, session resumed.
  // ssl.session_reused logged by both: client & server.
  testUtilV2(listener, client_ctx, client_session, true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "",
             "ssl.session_reused", 2, GetParam());

  // Connection without SNI succeeds, but session is NOT resumed on a different filter chain.
  client_ctx.clear_sni();
  testUtilV2(listener, client_ctx, client_session, true,
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e", "", "",
             "ssl.session_reused", 0, GetParam());

  // Connection to protected.example.com succeeds, but session is NOT resumed on a different
  // filter chain, even though it's serving the same certificate.
  client_ctx.set_sni("protected.example.com");
  testUtilV2(listener, client_ctx, client_session, true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "",
             "ssl.session_reused", 0, GetParam());

  // Connection to server2.example.com succeeds, session resumed on a different domain from
  // the same filter chain (respecting SNI restrictions) and serving the same certificate.
  // NOTE: Tested last to make sure that the session is still resumable.
  // ssl.session_reused logged by both: client & server.
  client_ctx.set_sni("server2.example.com");
  testUtilV2(listener, client_ctx, client_session, true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "",
             "ssl.session_reused", 2, GetParam());
}

TEST_P(SslConnectionImplTest, SniClientCertificate) {
  envoy::api::v2::Listener listener;

  // san_multiple_dns_cert.pem: *.example.com
  envoy::api::v2::FilterChain* filter_chain1 = listener.add_filter_chains();
  filter_chain1->mutable_filter_chain_match()->add_sni_domains("*.example.com");
  envoy::api::v2::TlsCertificate* server_cert1 =
      filter_chain1->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert1->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"));
  server_cert1->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem"));
  filter_chain1->mutable_tls_context()->mutable_session_ticket_keys()->add_keys()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"));

  // san_multiple_dns_cert.pem: protected.example.com
  // (same certificate as #1, but requires Client Certificate)
  envoy::api::v2::FilterChain* filter_chain2 = listener.add_filter_chains();
  filter_chain2->mutable_filter_chain_match()->add_sni_domains("protected.example.com");
  envoy::api::v2::TlsCertificate* server_cert2 =
      filter_chain2->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert2->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"));
  server_cert2->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem"));
  filter_chain2->mutable_tls_context()->mutable_session_ticket_keys()->add_keys()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"));
  filter_chain2->mutable_tls_context()->mutable_require_client_certificate()->set_value(true);
  envoy::api::v2::CertificateValidationContext* validation_ctx2 =
      filter_chain2->mutable_tls_context()
          ->mutable_common_tls_context()
          ->mutable_validation_context();
  validation_ctx2->add_verify_subject_alt_name("spiffe://lyft.com/bogus-team");
  validation_ctx2->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"));

  envoy::api::v2::UpstreamTlsContext client_ctx;

  // Connection to www.example.com succeeds.
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("www.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "",
             "ssl.handshake", 2, GetParam());

  // Connection to protected.example.com fails, since there is no client certificate.
  client_ctx.set_sni("protected.example.com");
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.fail_verify_no_cert", 1, GetParam());

  // Connection to protected.example.com with a valid client certificate fails, beacuse its SAN
  // is not whitelisted.
  envoy::api::v2::TlsCertificate* client_cert =
      client_ctx.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  client_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"));
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.fail_verify_san", 1, GetParam());

  // Connection to protected.example.com with a valid client certificate succeeds.
  // ssl.handshake logged by both: client & server.
  validation_ctx2->add_verify_subject_alt_name("spiffe://lyft.com/test-team");
  testUtilV2(listener, client_ctx, "", true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8",
             "spiffe://lyft.com/test-team", "", "ssl.handshake", 2, GetParam());

  // Connection to www.example.com with a valid client certificate succeeds,
  // but the client certificate is not requested.
  client_ctx.set_sni("www.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "",
             "ssl.no_certificate", 1, GetParam());
}

TEST_P(SslConnectionImplTest, SniSettingsParameters) {
  envoy::api::v2::Listener listener;

  // san_dns_cert.pem: server1.example.com
  envoy::api::v2::FilterChain* filter_chain1 = listener.add_filter_chains();
  filter_chain1->mutable_filter_chain_match()->add_sni_domains("server1.example.com");
  envoy::api::v2::TlsCertificate* server_cert1 =
      filter_chain1->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert1->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  server_cert1->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"));
  envoy::api::v2::CommonTlsContext* server_ctx1 =
      filter_chain1->mutable_tls_context()->mutable_common_tls_context();
  server_ctx1->add_alpn_protocols("srv1");
  server_ctx1->mutable_tls_params()->add_cipher_suites("ECDHE-RSA-CHACHA20-POLY1305");
  server_ctx1->mutable_tls_params()->add_ecdh_curves("X25519");

  // san_multiple_dns_cert.pem: server2.example.com
  envoy::api::v2::FilterChain* filter_chain2 = listener.add_filter_chains();
  filter_chain2->mutable_filter_chain_match()->add_sni_domains("server2.example.com");
  envoy::api::v2::TlsCertificate* server_cert2 =
      filter_chain2->mutable_tls_context()->mutable_common_tls_context()->add_tls_certificates();
  server_cert2->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"));
  server_cert2->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_key.pem"));
  envoy::api::v2::CommonTlsContext* server_ctx2 =
      filter_chain2->mutable_tls_context()->mutable_common_tls_context();
  server_ctx2->add_alpn_protocols("srv2");
  server_ctx2->mutable_tls_params()->add_cipher_suites("ECDHE-RSA-AES128-GCM-SHA256");
  server_ctx2->mutable_tls_params()->add_ecdh_curves("P-256");

  envoy::api::v2::UpstreamTlsContext client_ctx;

  // Connection to server1.example.com succeeds, negotiated ALPN is "srv1".
  // ssl.handshake logged by both: client & server.
  client_ctx.mutable_common_tls_context()->add_alpn_protocols("srv1");
  client_ctx.mutable_common_tls_context()->add_alpn_protocols("srv2");
  client_ctx.set_sni("server1.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e", "", "srv1",
             "ssl.handshake", 2, GetParam());

  // Connection to server2.example.com succeeds, negotiated ALPN is "srv2".
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("server2.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "srv2",
             "ssl.handshake", 2, GetParam());

  // Connection to server1.example.com using ECDHE-RSA-CHACHA20-POLY1305 succeeds.
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("server1.example.com");
  client_ctx.mutable_common_tls_context()->mutable_tls_params()->add_cipher_suites(
      "ECDHE-RSA-CHACHA20-POLY1305");
  testUtilV2(listener, client_ctx, "", true,
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e", "", "srv1",
             "ssl.handshake", 2, GetParam());

  // Connection to server2.example.com using ECDHE-RSA-CHACHA20-POLY1305 fails.
  client_ctx.set_sni("server2.example.com");
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.connection_error", 1, GetParam());

  // Connection to server1.example.com using ECDHE-RSA-AES128-GCM-SHA256 fails.
  client_ctx.set_sni("server1.example.com");
  client_ctx.mutable_common_tls_context()->mutable_tls_params()->clear_cipher_suites();
  client_ctx.mutable_common_tls_context()->mutable_tls_params()->add_cipher_suites(
      "ECDHE-RSA-AES128-GCM-SHA256");
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.connection_error", 1, GetParam());

  // Connection to server2.example.com using ECDHE-RSA-AES128-GCM-SHA256 succeeds.
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("server2.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "srv2",
             "ssl.handshake", 2, GetParam());

  client_ctx.mutable_common_tls_context()->mutable_tls_params()->clear_cipher_suites();

  // Connection to server1.example.com using X25519 succeeds.
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("server1.example.com");
  client_ctx.mutable_common_tls_context()->mutable_tls_params()->add_ecdh_curves("X25519");
  testUtilV2(listener, client_ctx, "", true,
             "1406294e80c818158697d65d2aaca16748ff132442ab0e2f28bc1109f1d47a2e", "", "srv1",
             "ssl.handshake", 2, GetParam());

  // Connection to server2.example.com using X25519 fails.
  client_ctx.set_sni("server2.example.com");
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.connection_error", 1, GetParam());

  // Connection to server1.example.com using P-256 fails.
  client_ctx.set_sni("server1.example.com");
  client_ctx.mutable_common_tls_context()->mutable_tls_params()->clear_ecdh_curves();
  client_ctx.mutable_common_tls_context()->mutable_tls_params()->add_ecdh_curves("P-256");
  testUtilV2(listener, client_ctx, "", false, "", "", "", "ssl.connection_error", 1, GetParam());

  // Connection to server2.example.com using P-256 succeeds.
  // ssl.handshake logged by both: client & server.
  client_ctx.set_sni("server2.example.com");
  testUtilV2(listener, client_ctx, "", true,
             "77b3c289abbded6ad508d9853ba0bd36a1f6a9680eaba01e0f32774c0676ebe8", "", "srv2",
             "ssl.handshake", 2, GetParam());

  client_ctx.mutable_common_tls_context()->mutable_tls_params()->clear_ecdh_curves();
}

class SslReadBufferLimitTest : public SslCertsTest,
                               public testing::WithParamInterface<Network::Address::IpVersion> {
public:
  void initialize(uint32_t read_buffer_limit) {
    server_ctx_loader_ = TestEnvironment::jsonLoadFromString(server_ctx_json_);
    server_ctx_config_.reset(new ServerContextConfigImpl(*server_ctx_loader_));
    manager_.reset(new ContextManagerImpl(runtime_));
    server_ctx_ = manager_->createSslServerContext("", {}, stats_store_, *server_ctx_config_, true);

    listener_ = dispatcher_->createSslListener(
        connection_handler_, *server_ctx_, socket_, listener_callbacks_, stats_store_,
        {.bind_to_port_ = true,
         .use_proxy_proto_ = false,
         .use_original_dst_ = false,
         .per_connection_buffer_limit_bytes_ = read_buffer_limit});

    client_ctx_loader_ = TestEnvironment::jsonLoadFromString(client_ctx_json_);
    client_ctx_config_.reset(new ClientContextConfigImpl(*client_ctx_loader_));
    client_ctx_ = manager_->createSslClientContext(stats_store_, *client_ctx_config_);

    client_connection_ = dispatcher_->createSslClientConnection(
        *client_ctx_, socket_.localAddress(), source_address_);
    client_connection_->addConnectionCallbacks(client_callbacks_);
    client_connection_->connect();
    read_filter_.reset(new Network::MockReadFilter());
  }

  void readBufferLimitTest(uint32_t read_buffer_limit, uint32_t expected_chunk_size,
                           uint32_t write_size, uint32_t num_writes, bool reserve_write_space) {
    initialize(read_buffer_limit);

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
    EXPECT_CALL(*read_filter_, onData(_))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Network::FilterStatus {
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

      client_connection_->write(data);
    }

    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_EQ(0UL, stats_store_.counter("ssl.connection_error").value());
  }

  void singleWriteTest(uint32_t read_buffer_limit, uint32_t bytes_to_write) {
    MockWatermarkBuffer* client_write_buffer = nullptr;
    MockBufferFactory* factory = new StrictMock<MockBufferFactory>;
    dispatcher_.reset(new Event::DispatcherImpl(Buffer::WatermarkFactoryPtr{factory}));

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

    initialize(read_buffer_limit);

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

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
    EXPECT_CALL(*read_filter_, onData(_)).Times(testing::AnyNumber());

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
    client_connection_->write(buffer_to_write);
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

  Stats::IsolatedStoreImpl stats_store_;
  Event::DispatcherPtr dispatcher_{new Event::DispatcherImpl};
  Network::TcpListenSocket socket_{Network::Test::getCanonicalLoopbackAddress(GetParam()), true};
  Network::MockListenerCallbacks listener_callbacks_;
  Network::MockConnectionHandler connection_handler_;
  std::string server_ctx_json_ = R"EOF(
    {
      "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
      "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
      "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
    }
    )EOF";
  std::string client_ctx_json_ = R"EOF(
    {
      "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
      "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
    }
  )EOF";
  Runtime::MockLoader runtime_;
  Json::ObjectSharedPtr server_ctx_loader_;
  std::unique_ptr<ServerContextConfigImpl> server_ctx_config_;
  std::unique_ptr<ContextManagerImpl> manager_;
  ServerContextPtr server_ctx_;
  Network::ListenerPtr listener_;
  Json::ObjectSharedPtr client_ctx_loader_;
  std::unique_ptr<ClientContextConfigImpl> client_ctx_config_;
  ClientContextPtr client_ctx_;
  Network::ClientConnectionPtr client_connection_;
  Network::ConnectionPtr server_connection_;
  NiceMock<Network::MockConnectionCallbacks> server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  StrictMock<Network::MockConnectionCallbacks> client_callbacks_;
  Network::Address::InstanceConstSharedPtr source_address_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, SslReadBufferLimitTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

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

  initialize(0);

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

  EXPECT_EQ(address_string, server_connection_->remoteAddress().ip()->addressAsString());

  disconnect();
}

} // namespace Ssl
} // namespace Envoy
