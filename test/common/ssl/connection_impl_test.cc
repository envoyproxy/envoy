#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
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
  ServerContextPtr server_ctx(manager.createSslServerContext(stats_store, server_ctx_config));

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
  ServerContextPtr server_ctx(manager.createSslServerContext(stats_store, server_ctx_config));

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
  ServerContextPtr server_ctx1(manager.createSslServerContext(stats_store, server_ctx_config1));
  ServerContextPtr server_ctx2(manager.createSslServerContext(stats_store, server_ctx_config2));

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
  ServerContextPtr server_ctx(manager.createSslServerContext(stats_store, server_ctx_config));
  ServerContextPtr server2_ctx(manager.createSslServerContext(stats_store, server2_ctx_config));

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
  ServerContextPtr server_ctx(manager.createSslServerContext(stats_store, server_ctx_config));

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

class SslReadBufferLimitTest : public SslCertsTest,
                               public testing::WithParamInterface<Network::Address::IpVersion> {
public:
  void initialize(uint32_t read_buffer_limit) {
    server_ctx_loader_ = TestEnvironment::jsonLoadFromString(server_ctx_json_);
    server_ctx_config_.reset(new ServerContextConfigImpl(*server_ctx_loader_));
    manager_.reset(new ContextManagerImpl(runtime_));
    server_ctx_ = manager_->createSslServerContext(stats_store_, *server_ctx_config_);

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
