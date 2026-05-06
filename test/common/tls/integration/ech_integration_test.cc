#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "source/common/common/base64.h"
#include "source/common/ssl/ssl.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/ssl_handshaker.h"

#include "test/common/tls/integration/ssl_integration_test_base.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/evp.h"
#include "openssl/ssl.h"

using testing::ReturnRef;

namespace Envoy {
namespace Ssl {

// Helper that generates a fresh HPKE keypair plus matching ECHConfig / ECHConfigList bytes.
struct EchTestData {
  std::string private_key_bytes;
  std::string ech_config_bytes;
  std::string ech_config_list_bytes;
};

static EchTestData generateEchTestData() {
  EchTestData data;

  bssl::ScopedEVP_HPKE_KEY key;
  EXPECT_EQ(1, EVP_HPKE_KEY_generate(key.get(), EVP_hpke_x25519_hkdf_sha256()));

  uint8_t priv_buf[32];
  size_t priv_len = 0;
  EXPECT_EQ(1, EVP_HPKE_KEY_private_key(key.get(), priv_buf, &priv_len, sizeof(priv_buf)));
  data.private_key_bytes.assign(reinterpret_cast<char*>(priv_buf), priv_len);

  // Marshal ECHConfig.
  bssl::ScopedCBB cbb;
  EXPECT_EQ(1, CBB_init(cbb.get(), 128));
  EXPECT_EQ(1, SSL_marshal_ech_config(cbb.get(), /*config_id=*/1, key.get(), "public.example.com",
                                      /*max_name_len=*/255));
  uint8_t* ech_buf = nullptr;
  size_t ech_len = 0;
  EXPECT_EQ(1, CBB_finish(cbb.get(), &ech_buf, &ech_len));
  data.ech_config_bytes.assign(reinterpret_cast<char*>(ech_buf), ech_len);
  OPENSSL_free(ech_buf);

  // Build SSL_ECH_KEYS so we can marshal the ECHConfigList via the retry-configs API.
  bssl::UniquePtr<SSL_ECH_KEYS> ech_keys(SSL_ECH_KEYS_new());
  EXPECT_EQ(1, SSL_ECH_KEYS_add(ech_keys.get(), /*is_retry_config=*/0,
                                reinterpret_cast<const uint8_t*>(data.ech_config_bytes.data()),
                                data.ech_config_bytes.size(), key.get()));

  bssl::ScopedCBB cbb2;
  EXPECT_EQ(1, CBB_init(cbb2.get(), 128));
  EXPECT_EQ(1, SSL_ECH_KEYS_marshal_retry_configs(ech_keys.get(), cbb2.get()));
  uint8_t* list_buf = nullptr;
  size_t list_len = 0;
  EXPECT_EQ(1, CBB_finish(cbb2.get(), &list_buf, &list_len));
  data.ech_config_list_bytes.assign(reinterpret_cast<char*>(list_buf), list_len);
  OPENSSL_free(list_buf);

  return data;
}

class EchIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public SslIntegrationTestBase {
public:
  EchIntegrationTest() : SslIntegrationTestBase(GetParam()) {}

  void TearDown() override { SslIntegrationTestBase::TearDown(); }

  // Initialize with ECH keys added to the server's DownstreamTlsContext.
  void initializeWithEchKeys(const EchTestData& data) {
    config_helper_.addSslConfig(ConfigHelper::ServerSslOptions().setRsaCert(true));

    // Capture by value so the lambda owns a copy of the key bytes.
    const std::string priv = data.private_key_bytes;
    const std::string ech_cfg = data.ech_config_bytes;

    config_helper_.addConfigModifier(
        [priv, ech_cfg](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* filter_chain =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
          auto* transport_socket = filter_chain->mutable_transport_socket();

          envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_ctx;
          transport_socket->typed_config().UnpackTo(&tls_ctx);

          auto* key_proto = tls_ctx.mutable_encrypted_client_hello()->mutable_server()->add_keys();
          key_proto->mutable_private_key()->set_inline_bytes(priv);
          key_proto->set_ech_config(ech_cfg);
          key_proto->set_is_retry_config(false);

          transport_socket->mutable_typed_config()->PackFrom(tls_ctx);
        });

    SslIntegrationTestBase::initialize();
  }

  // Create a client connection with ECH enabled using the given ECHConfigList bytes.
  Network::ClientConnectionPtr makeEchClientConnection(const std::string& ech_config_list_bytes) {
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    initializeUpstreamTlsContextConfig(ClientSslTransportOptions{}, tls_context);
    tls_context.mutable_encrypted_client_hello()->mutable_client()->set_ech_config_list(
        ech_config_list_bytes);

    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
    ON_CALL(mock_factory_ctx.server_context_, api()).WillByDefault(ReturnRef(*api_));
    auto cfg = *Extensions::TransportSockets::Tls::ClientContextConfigImpl::create(
        tls_context, mock_factory_ctx);
    static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
    auto factory = THROW_OR_RETURN_VALUE(
        Extensions::TransportSockets::Tls::ClientSslSocketFactory::create(
            std::move(cfg), *context_manager_, *client_stats_store->rootScope()),
        Network::UpstreamTransportSocketFactoryPtr);

    Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               factory->createTransportSocket({}, nullptr), nullptr,
                                               nullptr);
  }

  // Create a client connection with ECH GREASE enabled.
  Network::ClientConnectionPtr makeEchGreaseClientConnection() {
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    initializeUpstreamTlsContextConfig(ClientSslTransportOptions{}, tls_context);
    tls_context.mutable_encrypted_client_hello()->mutable_client()->set_enable_grease(true);

    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
    ON_CALL(mock_factory_ctx.server_context_, api()).WillByDefault(ReturnRef(*api_));
    auto cfg = *Extensions::TransportSockets::Tls::ClientContextConfigImpl::create(
        tls_context, mock_factory_ctx);
    static auto* grease_stats_store = new Stats::TestIsolatedStoreImpl();
    auto factory = THROW_OR_RETURN_VALUE(
        Extensions::TransportSockets::Tls::ClientSslSocketFactory::create(
            std::move(cfg), *context_manager_, *grease_stats_store->rootScope()),
        Network::UpstreamTransportSocketFactoryPtr);

    Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               factory->createTransportSocket({}, nullptr), nullptr,
                                               nullptr);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, EchIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that when a client sends an ECH-encrypted ClientHello and the server
// has the matching HPKE key, the handshake completes and echAccepted() is true
// on the client side.
BORINGSSL_TEST_P(EchIntegrationTest, ServerAcceptsEchFromClient) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.tls_encrypted_client_hello", "true"}});

  EchTestData data = generateEchTestData();
  initializeWithEchKeys(data);

  Network::ClientConnectionPtr connection = makeEchClientConnection(data.ech_config_list_bytes);
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  while (!callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Ssl::ConnectionInfoConstSharedPtr ssl_info = connection->ssl();
  ASSERT_NE(ssl_info, nullptr);
  EXPECT_TRUE(ssl_info->echAccepted());

  connection->close(Network::ConnectionCloseType::NoFlush);
}

// Verify that when the runtime flag is off, ECH config on the client is ignored
// and the connection still succeeds (no ECH, but TLS handshake completes normally).
BORINGSSL_TEST_P(EchIntegrationTest, RuntimeFlagOffSkipsEch) {
  // Runtime flag stays off (default FALSE_RUNTIME_GUARD).
  EchTestData data = generateEchTestData();
  initializeWithEchKeys(data);

  // Client has ECH configured but runtime flag is off — ECH won't be sent.
  Network::ClientConnectionPtr connection = makeEchClientConnection(data.ech_config_list_bytes);
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  while (!callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Ssl::ConnectionInfoConstSharedPtr ssl_info = connection->ssl();
  ASSERT_NE(ssl_info, nullptr);
  EXPECT_FALSE(ssl_info->echAccepted());

  connection->close(Network::ConnectionCloseType::NoFlush);
}

// Verify that a client sending ECH GREASE (no real ECHConfigList) connects
// successfully but echAccepted() returns false (no real ECH was performed).
BORINGSSL_TEST_P(EchIntegrationTest, ClientEchGreaseConnectsSuccessfully) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.tls_encrypted_client_hello", "true"}});

  EchTestData data = generateEchTestData();
  initializeWithEchKeys(data);

  // Client sends GREASE: no real ECHConfigList, server has ECH keys.
  Network::ClientConnectionPtr connection = makeEchGreaseClientConnection();
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();
  while (!callbacks.connected()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Ssl::ConnectionInfoConstSharedPtr ssl_info = connection->ssl();
  ASSERT_NE(ssl_info, nullptr);
  // GREASE does not result in a real ECH acceptance.
  EXPECT_FALSE(ssl_info->echAccepted());

  connection->close(Network::ConnectionCloseType::NoFlush);
}

// Verify that when the client provides an ECHConfigList with a config_id that
// does not match any server key, the server supplies retry configs and the
// connection uses the outer (unencrypted) SNI.
BORINGSSL_TEST_P(EchIntegrationTest, ServerProvidesRetryConfigsOnConfigIdMismatch) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.tls_encrypted_client_hello", "true"}});

  // Server is configured with key A.
  EchTestData server_data = generateEchTestData();
  initializeWithEchKeys(server_data);

  // Client uses ECHConfigList from a *different* keypair (mismatch).
  EchTestData client_data = generateEchTestData();
  Network::ClientConnectionPtr connection =
      makeEchClientConnection(client_data.ech_config_list_bytes);
  ConnectionStatusCallbacks callbacks;
  connection->addConnectionCallbacks(callbacks);
  connection->connect();

  // With a config_id mismatch, BoringSSL will retry ECH using the retry
  // configs provided by the server. The connection may still succeed or
  // fail depending on BoringSSL's handling; what matters is that retry
  // configs are non-empty on the server side.
  while (!callbacks.connected() && !callbacks.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Ssl::ConnectionInfoConstSharedPtr ssl_info = connection->ssl();
  ASSERT_NE(ssl_info, nullptr);
  // ECH was not accepted because the config_id did not match.
  EXPECT_FALSE(ssl_info->echAccepted());
  // Retry configs should be available (server returned its public ECHConfigList).
  EXPECT_FALSE(ssl_info->echRetryConfigs().empty());

  connection->close(Network::ConnectionCloseType::NoFlush);
}

} // namespace Ssl
} // namespace Envoy
