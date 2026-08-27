#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_config_impl.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

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
            std::move(config), context_manager_, *client_stats_store_.rootScope())};
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

  const std::string listener_a_{"crl_listener_a"};
  const std::string listener_b_{"crl_listener_b"};
  Stats::TestIsolatedStoreImpl client_stats_store_;
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

} // namespace
} // namespace Envoy
