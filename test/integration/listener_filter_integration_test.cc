#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"

#include "common/config/api_version.h"
#include "common/network/utility.h"

#include "extensions/filters/listener/tls_inspector/tls_inspector.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/mocks/secret/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ListenerFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public BaseIntegrationTest {
public:
  ListenerFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
      filters:
       -  name: envoy.filters.network.echo
)EOF") {}

  ~ListenerFilterIntegrationTest() override = default;
  std::string appendMatcher(const std::string& listener_filter, bool disabled) {
    if (disabled) {
      return listener_filter +
             R"EOF(
filter_disabled:
  any_match: true
)EOF";
    } else {
      return listener_filter +
             R"EOF(
filter_disabled:
  not_match:
    any_match: true
)EOF";
    }
  }

  void initializeWithListenerFilter(absl::optional<bool> listener_filter_disabled = absl::nullopt) {
    config_helper_.renameListener("echo");
    std::string tls_inspector_config = ConfigHelper::tlsInspectorFilter();
    if (listener_filter_disabled.has_value()) {
      tls_inspector_config = appendMatcher(tls_inspector_config, listener_filter_disabled.value());
    }
    config_helper_.addListenerFilter(tls_inspector_config);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto* alpn = filter_chain->mutable_filter_chain_match()->add_application_protocols();
      *alpn = "envoyalpn";
    });
    config_helper_.addSslConfig();
    BaseIntegrationTest::initialize();

    context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  }

  void setupConnections(bool listener_filter_disabled, bool expect_connection_open) {
    initializeWithListenerFilter(listener_filter_disabled);

    // Set up the SSL client.
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("echo"));
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
    ssl_client_ = dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        context_->createTransportSocket(
            // nullptr
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(),
                std::vector<std::string>{"envoyalpn"})),
        nullptr);
    ssl_client_->addConnectionCallbacks(connect_callbacks_);
    ssl_client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }

    if (expect_connection_open) {
      ASSERT(connect_callbacks_.connected());
      ASSERT_FALSE(connect_callbacks_.closed());
    } else {
      ASSERT_FALSE(connect_callbacks_.connected());
      ASSERT(connect_callbacks_.closed());
    }
  }
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::ClientConnectionPtr ssl_client_;
};

// Each listener filter is enabled by default.
TEST_P(ListenerFilterIntegrationTest, AllListenerFiltersAreEnabledByDefault) {
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true);
  ssl_client_->close(Network::ConnectionCloseType::NoFlush);
}

// The tls_inspector is disabled. The ALPN won't be sniffed out and no filter chain is matched.
TEST_P(ListenerFilterIntegrationTest, DisabledTlsInspectorFailsFilterChainFind) {
  setupConnections(/*listener_filter_disabled=*/true, /*expect_connection_open=*/false);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ListenerFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
} // namespace
} // namespace Envoy
