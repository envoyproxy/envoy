#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/tls/cert_validator/default_validator.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_config_impl.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Two downstream mTLS listeners that reference the same CRL content exercise the process-wide
// CrlCache (source/common/tls/cert_validator/default_validator.h), which parses the CRL once and
// shares the parsed representation across TLS contexts. This test validates that removing (or
// re-pointing) the config for one of the sharing listeners keeps the CRL valid and enforced for the
// surviving listener, and that tearing down both listeners is safe. It is primarily intended to run
// under ASAN/TSAN to catch lifetime regressions in the shared-CRL handling.
class ListenerCrlShareIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  ListenerCrlShareIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // The listeners use custom stat prefixes that have no tag-extraction rules.
    skip_tag_extraction_rule_check_ = true;
    ON_CALL(factory_context_.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  }

  void initialize() override {
    // Replace the single default listener with two listeners that carry an identical downstream
    // mTLS transport socket referencing the same CRL, so the running server shares one parsed CRL
    // between them.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
      ASSERT(listeners->size() == 1);
      const envoy::config::listener::v3::Listener base = listeners->Get(0);
      listeners->Clear();
      *listeners->Add() = makeMtlsListener(base, listener_a_);
      *listeners->Add() = makeMtlsListener(base, listener_b_);
    });
    HttpIntegrationTest::initialize();
  }

  // Builds a copy of `base` bound to its own port with a downstream mTLS transport socket that
  // requires a client certificate and enforces revocation through the shared CRL.
  envoy::config::listener::v3::Listener
  makeMtlsListener(const envoy::config::listener::v3::Listener& base, const std::string& name) {
    envoy::config::listener::v3::Listener listener = base;
    listener.set_name(name);
    // Set an explicit stat prefix so the SSL stats are addressed as `listener.<name>.ssl.*`.
    listener.set_stat_prefix(name);
    auto* transport_socket = listener.mutable_filter_chains(0)->mutable_transport_socket();
    transport_socket->set_name("envoy.transport_sockets.tls");
    std::ignore = transport_socket->mutable_typed_config()->PackFrom(makeDownstreamMtlsContext());
    return listener;
  }

  // Downstream mTLS context that presents the unittest server certificate, requires a client
  // certificate and points at ca_cert.crl. Both listeners use identical content so the shared CRL
  // cache holds a single parsed copy.
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext makeDownstreamMtlsContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    tls_context.mutable_require_client_certificate()->set_value(true);
    auto* common = tls_context.mutable_common_tls_context();
    auto* cert = common->add_tls_certificates();
    cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/common/tls/test_data/unittest_cert.pem"));
    cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/common/tls/test_data/unittest_key.pem"));
    auto* validation = common->mutable_validation_context();
    validation->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/common/tls/test_data/ca_cert.pem"));
    validation->mutable_crl()->set_filename(
        TestEnvironment::runfilesPath("test/common/tls/test_data/ca_cert.crl"));
    return tls_context;
  }

  // Creates a client transport socket factory that presents the given client certificate. The
  // client does not verify the server, mirroring the CRL unit tests in ssl_socket_test.cc.
  Network::UpstreamTransportSocketFactoryPtr makeClientSslFactory(const std::string& cert,
                                                                  const std::string& key) {
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    auto* client_cert = tls_context.mutable_common_tls_context()->add_tls_certificates();
    client_cert->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(cert));
    client_cert->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(key));
    auto config = *Extensions::TransportSockets::Tls::ClientContextConfigImpl::create(
        tls_context, factory_context_);
    return Network::UpstreamTransportSocketFactoryPtr{
        *Extensions::TransportSockets::Tls::ClientSslSocketFactory::create(
            std::move(config), context_manager_, server_factory_context_.serverScope())};
  }

  // Attempts an mTLS handshake to the named listener with the given client factory and returns
  // whether the TLS handshake succeeded.
  bool handshakeConnected(const std::string& listener_name,
                          Network::UpstreamTransportSocketFactoryPtr& client_ssl) {
    Network::ClientConnectionPtr connection = dispatcher_->createClientConnection(
        Ssl::getSslAddress(version_, lookupPort(listener_name)),
        Network::Address::InstanceConstSharedPtr(),
        client_ssl->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
    IntegrationCodecClientPtr codec = makeRawHttpConnection(std::move(connection), std::nullopt);
    const bool connected = codec->connected();
    codec->connection()->close(Network::ConnectionCloseType::NoFlush);
    return connected;
  }

  // Rewrites the file-based LDS config to contain only the named listeners, deleting the rest.
  void keepOnlyListeners(const std::vector<std::string>& keep_names, absl::string_view version) {
    ConfigHelper new_config(version_, config_helper_.bootstrap());
    new_config.addConfigModifier([&keep_names](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
      for (int i = listeners->size() - 1; i >= 0; --i) {
        if (std::find(keep_names.begin(), keep_names.end(), listeners->Get(i).name()) ==
            keep_names.end()) {
          listeners->DeleteSubrange(i, 1);
        }
      }
    });
    new_config.setLds(version);
  }

  // The server's process-wide parsed PEM caches.
  struct PemCaches {
    std::shared_ptr<Extensions::TransportSockets::Tls::CrlCache> crl;
    std::shared_ptr<Extensions::TransportSockets::Tls::CaCertCache> ca;
    std::shared_ptr<Extensions::TransportSockets::Tls::CertChainCache> cert_chain;
    std::shared_ptr<Extensions::TransportSockets::Tls::PrivateKeyCache> private_key;

    bool allHaveSize(size_t expected) const {
      return crl->size() == expected && ca->size() == expected && cert_chain->size() == expected &&
             private_key->size() == expected;
    }
  };

  // Looks up the server's caches on its main thread, where the singleton manager lives. Their
  // sizes can then be read from the test thread.
  PemCaches serverPemCaches() {
    PemCaches caches;
    absl::Notification done;
    test_server_->server().dispatcher().post([this, &caches, &done]() {
      Singleton::Manager& manager = test_server_->server().singletonManager();
      caches.crl = Extensions::TransportSockets::Tls::getCrlCache(manager);
      caches.ca = Extensions::TransportSockets::Tls::getCaCertCache(manager);
      caches.cert_chain = Extensions::TransportSockets::Tls::getCertChainCache(manager);
      caches.private_key = Extensions::TransportSockets::Tls::getPrivateKeyCache(manager);
      done.Notify();
    });
    done.WaitForNotification();
    return caches;
  }

  // Waits until every cache holds `expected` entries. A removed listener's TLS contexts are
  // released asynchronously, possibly on a worker thread.
  void waitForCacheSizes(const PemCaches& caches, size_t expected) {
    for (int i = 0; i < 1000 && !caches.allHaveSize(expected); ++i) {
      timeSystem().advanceTimeWait(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(caches.crl->size(), expected);
    EXPECT_EQ(caches.ca->size(), expected);
    EXPECT_EQ(caches.cert_chain->size(), expected);
    EXPECT_EQ(caches.private_key->size(), expected);
  }

  const std::string listener_a_{"crl_listener_a"};
  const std::string listener_b_{"crl_listener_b"};
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ListenerCrlShareIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Two listeners share one parsed CRL. Deleting the config for one leaves the CRL valid and still
// enforced for the other, and then deleting the surviving listener releases the shared CRL without
// crashing.
TEST_P(ListenerCrlShareIntegrationTest, SharedCrlSurvivesListenerDeletion) {
  // Use a short drain time so a removed listener (and its TLS context) is torn down promptly while
  // the sharing listener is still active.
  setDrainTime(std::chrono::seconds(1));
  initialize();
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(2));

  auto revoked_client = makeClientSslFactory("test/common/tls/test_data/san_dns_cert.pem",
                                             "test/common/tls/test_data/san_dns_key.pem");
  auto valid_client = makeClientSslFactory("test/common/tls/test_data/san_dns2_cert.pem",
                                           "test/common/tls/test_data/san_dns2_key.pem");

  // Both listeners share the same parsed CRL. Listener B rejects the revoked client certificate and
  // accepts the unrevoked one.
  EXPECT_FALSE(handshakeConnected(listener_b_, revoked_client));
  test_server_->waitForCounter("listener." + listener_b_ + ".ssl.fail_verify_error",
                               testing::Ge(1));
  EXPECT_TRUE(handshakeConnected(listener_b_, valid_client));

  // Delete listener A's config. Its TLS context (an owner of the shared CRL) is destroyed while
  // listener B keeps using the CRL. This is the primary lifetime path exercised under ASAN/TSAN.
  keepOnlyListeners({listener_b_}, "1");
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(1));
  test_server_->waitForGauge("listener_manager.total_listeners_draining", testing::Eq(0));

  // The surviving listener still enforces revocation through the shared CRL.
  EXPECT_FALSE(handshakeConnected(listener_b_, revoked_client));
  EXPECT_TRUE(handshakeConnected(listener_b_, valid_client));

  // Delete listener B as well. Releasing the last owner of the shared CRL must not crash.
  keepOnlyListeners({}, "2");
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(0));
  test_server_->waitForGauge("listener_manager.total_listeners_draining", testing::Eq(0));
}

// A variant that re-points one listener at a different CRL rather than deleting it. The listener
// that keeps the original CRL must be unaffected, and tearing everything down must not crash.
TEST_P(ListenerCrlShareIntegrationTest, SharedCrlSurvivesListenerCrlChange) {
  setDrainTime(std::chrono::seconds(1));
  initialize();
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(2));

  auto revoked_client = makeClientSslFactory("test/common/tls/test_data/san_dns_cert.pem",
                                             "test/common/tls/test_data/san_dns_key.pem");
  auto valid_client = makeClientSslFactory("test/common/tls/test_data/san_dns2_cert.pem",
                                           "test/common/tls/test_data/san_dns2_key.pem");

  EXPECT_FALSE(handshakeConnected(listener_b_, revoked_client));
  EXPECT_TRUE(handshakeConnected(listener_b_, valid_client));

  // Re-point listener A at a different CRL. Listener A stops sharing with B, which now solely owns
  // the original parsed CRL; B must keep enforcing it exactly as before.
  ConfigHelper new_config(version_, config_helper_.bootstrap());
  new_config.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
    for (int i = 0; i < listeners->size(); ++i) {
      if (listeners->Get(i).name() != listener_a_) {
        continue;
      }
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context =
          makeDownstreamMtlsContext();
      tls_context.mutable_common_tls_context()
          ->mutable_validation_context()
          ->mutable_crl()
          ->set_filename(
              TestEnvironment::runfilesPath("test/common/tls/test_data/intermediate_ca_cert.crl"));
      auto* transport_socket =
          listeners->Mutable(i)->mutable_filter_chains(0)->mutable_transport_socket();
      std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
    }
  });
  new_config.setLds("1");
  test_server_->waitForCounter("listener_manager.listener_modified", testing::Ge(1));

  // Listener B, still on the original CRL, keeps rejecting the revoked cert and accepting the
  // valid.
  EXPECT_FALSE(handshakeConnected(listener_b_, revoked_client));
  EXPECT_TRUE(handshakeConnected(listener_b_, valid_client));

  // Tear everything down; releasing both CRLs must not crash.
  keepOnlyListeners({}, "2");
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(0));
}

// The two listeners share one parsed CRL, CA bundle, certificate chain and private key. Each shared
// entry stays cached while any listener references it and is erased from its cache as soon as the
// last listener using it is removed. Re-adding the listeners parses and caches the material again.
TEST_P(ListenerCrlShareIntegrationTest, SharedPemEntriesReleasedWithLastListener) {
  setDrainTime(std::chrono::seconds(1));
  initialize();
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(2));

  auto revoked_client = makeClientSslFactory("test/common/tls/test_data/san_dns_cert.pem",
                                             "test/common/tls/test_data/san_dns_key.pem");
  auto valid_client = makeClientSslFactory("test/common/tls/test_data/san_dns2_cert.pem",
                                           "test/common/tls/test_data/san_dns2_key.pem");

  const PemCaches caches = serverPemCaches();
  waitForCacheSizes(caches, 1);
  EXPECT_TRUE(handshakeConnected(listener_a_, valid_client));
  EXPECT_TRUE(handshakeConnected(listener_b_, valid_client));

  // Removing one listener keeps every entry cached for the other, which still enforces the CRL.
  keepOnlyListeners({listener_b_}, "1");
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(1));
  test_server_->waitForGauge("listener_manager.total_listeners_draining", testing::Eq(0));
  waitForCacheSizes(caches, 1);
  EXPECT_FALSE(handshakeConnected(listener_b_, revoked_client));
  EXPECT_TRUE(handshakeConnected(listener_b_, valid_client));

  // Removing the last listener releases every entry, which erases it from its cache.
  EXPECT_LOG_CONTAINS("debug", "tls: released parsed PEM entry, 0 entries", {
    keepOnlyListeners({}, "2");
    test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(0));
    test_server_->waitForGauge("listener_manager.total_listeners_draining", testing::Eq(0));
    waitForCacheSizes(caches, 0);
  });

  // Re-adding both listeners parses the material once more and shares it between them.
  EXPECT_LOG_CONTAINS("debug", "tls: cached parsed PEM entry, 1 entries", {
    ConfigHelper new_config(version_, config_helper_.bootstrap());
    new_config.setLds("3");
    test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(2));
  });
  waitForCacheSizes(caches, 1);
  // The re-added listeners are bound to new ports.
  registerTestServerPorts({listener_a_, listener_b_});
  EXPECT_FALSE(handshakeConnected(listener_a_, revoked_client));
  EXPECT_TRUE(handshakeConnected(listener_a_, valid_client));
  EXPECT_TRUE(handshakeConnected(listener_b_, valid_client));
}

// A connection that is still open when its listener is removed holds a reference to the shared
// entries through its TLS socket. The drain closes the connection on its worker before the listener
// is destroyed, after which every entry is erased.
TEST_P(ListenerCrlShareIntegrationTest, SharedPemEntriesReleasedAfterOpenConnectionDrains) {
  setDrainTime(std::chrono::seconds(1));
  initialize();
  test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(2));

  auto valid_client = makeClientSslFactory("test/common/tls/test_data/san_dns2_cert.pem",
                                           "test/common/tls/test_data/san_dns2_key.pem");
  const PemCaches caches = serverPemCaches();
  waitForCacheSizes(caches, 1);

  Network::ClientConnectionPtr connection = dispatcher_->createClientConnection(
      Ssl::getSslAddress(version_, lookupPort(listener_b_)),
      Network::Address::InstanceConstSharedPtr(),
      valid_client->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  IntegrationCodecClientPtr codec = makeRawHttpConnection(std::move(connection), std::nullopt);
  ASSERT_TRUE(codec->connected());
  test_server_->waitForGauge("listener." + listener_b_ + ".downstream_cx_active", testing::Eq(1));

  EXPECT_LOG_CONTAINS("debug", "tls: released parsed PEM entry, 0 entries", {
    keepOnlyListeners({}, "1");
    test_server_->waitForGauge("listener_manager.total_listeners_active", testing::Eq(0));
    test_server_->waitForGauge("listener_manager.total_listeners_draining", testing::Eq(0));
    // The listener's removal closes the connection.
    ASSERT_TRUE(codec->waitForDisconnect());
    waitForCacheSizes(caches, 0);
  });
}

} // namespace
} // namespace Envoy
