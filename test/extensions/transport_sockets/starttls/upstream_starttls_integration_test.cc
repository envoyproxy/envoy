#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/connection_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/transport_sockets/starttls/starttls_socket.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/config/utility.h"
#include "test/extensions/transport_sockets/starttls/starttls_integration_test.pb.h"
#include "test/extensions/transport_sockets/starttls/starttls_integration_test.pb.validate.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {

// Simple filter for test purposes. This filter will be injected into the filter chain during
// tests.
// The filter reacts only to "switch" keyword to switch upstream startTls transport socket to secure
// mode. All other payloads are forwarded either downstream or upstream respectively.
// Filter will be instantiated as terminal filter in order to have access to upstream connection.
class StartTlsSwitchFilter : public Network::Filter {
public:
  ~StartTlsSwitchFilter() override {
    if (upstream_connection_) {
      upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
    }
  }

  void onCommand(Buffer::Instance& data);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  Network::FilterStatus onNewConnection() override;

  static std::shared_ptr<StartTlsSwitchFilter>
  newInstance(Upstream::ClusterManager& cluster_manager) {
    auto p = std::shared_ptr<StartTlsSwitchFilter>(new StartTlsSwitchFilter(cluster_manager));
    p->self_ = p;
    return p;
  }

  // Helper filter to catch onData coming on upstream connection.
  struct UpstreamReadFilter : public Network::ReadFilter {
    UpstreamReadFilter(std::weak_ptr<StartTlsSwitchFilter> parent) : parent_(parent) {}

    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      if (auto parent = parent_.lock()) {
        parent->onCommand(data);
        return parent->onWrite(data, end_stream);
      } else {
        read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
        return Network::FilterStatus::StopIteration;
      }
    }

    Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

    void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
      read_callbacks_ = &callbacks;
    }

    std::weak_ptr<StartTlsSwitchFilter> parent_{};
    Network::ReadFilterCallbacks* read_callbacks_{};
  };

private:
  StartTlsSwitchFilter(Upstream::ClusterManager& cluster_manager)
      : cluster_manager_(cluster_manager) {}

  std::weak_ptr<StartTlsSwitchFilter> self_{};
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  Network::ClientConnectionPtr upstream_connection_{};
  Upstream::ClusterManager& cluster_manager_;
};

Network::FilterStatus StartTlsSwitchFilter::onNewConnection() {
  auto c = cluster_manager_.getThreadLocalCluster("cluster_0");
  auto h = c->loadBalancer().chooseHost(nullptr);
  upstream_connection_ =
      h->createConnection(read_callbacks_->connection().dispatcher(), nullptr, nullptr).connection_;
  upstream_connection_->addReadFilter(std::make_shared<UpstreamReadFilter>(self_));
  upstream_connection_->connect();
  return Network::FilterStatus::Continue;
}

Network::FilterStatus StartTlsSwitchFilter::onData(Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    upstream_connection_->close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }

  onCommand(data);
  upstream_connection_->write(data, end_stream);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus StartTlsSwitchFilter::onWrite(Buffer::Instance& buf, bool end_stream) {
  read_callbacks_->connection().write(buf, end_stream);
  return Network::FilterStatus::Continue;
}

void StartTlsSwitchFilter::onCommand(Buffer::Instance& buf) {
  const std::string message = buf.toString();
  if (message == "switch") {
    // Start the upstream secure transport immediately since we clearly have all the bytes
    ASSERT_TRUE(upstream_connection_->startSecureTransport());
  }
}

// Config factory for StartTlsSwitchFilter terminal filter.
class StartTlsSwitchFilterConfigFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                              test::integration::starttls::StartTlsFilterConfig> {
public:
  explicit StartTlsSwitchFilterConfigFactory(const std::string& name) : FactoryBase(name) {}
  bool isTerminalFilterByProtoTyped(const test::integration::starttls::StartTlsFilterConfig&,
                                    Server::Configuration::FactoryContext&) override {
    return true;
  }

  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const test::integration::starttls::StartTlsFilterConfig&,
                                    Server::Configuration::FactoryContext& context) override {
    return [&](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(StartTlsSwitchFilter::newInstance(context.clusterManager()));
    };
  }

  std::string name() const override { return name_; }

private:
  const std::string name_;
};

// Fixture class for integration tests.
class StartTlsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public BaseIntegrationTest {
public:
  StartTlsIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig()),
        stream_info_(timeSystem(), nullptr) {}
  void initialize() override;

  // Config factory for StartTlsSwitchFilter.
  StartTlsSwitchFilterConfigFactory config_factory_{"startTls"};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_config_factory_{config_factory_};

  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::TransportSocketFactoryPtr tls_context_;

  // Technically unused.
  StreamInfo::StreamInfoImpl stream_info_;
};

void StartTlsIntegrationTest::initialize() {
  config_helper_.renameListener("starttls_test");

  // Modifications to ConfigHelper::baseConfig.
  // Add starttls transport socket to cluster_0
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    envoy::extensions::transport_sockets::starttls::v3::UpstreamStartTlsConfig starttls_config;
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext* tls_context =
        starttls_config.mutable_tls_socket_config();
    auto* tls_certificate = tls_context->mutable_common_tls_context()->add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.starttls");
    cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(starttls_config);
  });

  // Modifications to ConfigHelper::baseConfig.
  // Insert StartTlsSwitchFilter into filter chain.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()->mutable_listeners(0)->add_filter_chains();
    config_helper_.addNetworkFilter(R"EOF(
      name: startTls
      typed_config:
        "@type": type.googleapis.com/test.integration.starttls.StartTlsFilterConfig
    )EOF");
  });

  // Setup factory and context for tls transport socket.
  // The tls transport socket will be inserted into fake_upstream when
  // upstream starttls transport socket is converted to secure mode.
  tls_context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext downstream_tls_context;

  std::string yaml_plain = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/config/integration/certs/clientcert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/config/integration/certs/clientkey.pem"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_plain), downstream_tls_context);

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx, api()).WillByDefault(testing::ReturnRef(*api_));
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      downstream_tls_context, mock_factory_ctx);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  tls_context_ = Network::TransportSocketFactoryPtr{
      new Extensions::TransportSockets::Tls::ServerSslSocketFactory(
          std::move(cfg), *tls_context_manager_, *client_stats_store, {})};

  BaseIntegrationTest::initialize();
}

// Test creates a client connection to Envoy and passes some date to fake_upstream.
// After that fake_upstream is converted to use TLS transport socket.
// The client sends a special message to Envoy which causes upstream starttls
// transport socket to start using secure mode.
// The client sends a message to fake_upstream to test encrypted connection between
// Envoy and fake_upstream.
// Finally fake_upstream sends a message through encrypted connection to Envoy
// which is resent to the client in clear-text.
TEST_P(StartTlsIntegrationTest, SwitchToTlsFromClient) {
  initialize();

  // Open clear-text connection.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("starttls_test"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send a message to fake_upstream via Envoy.
  ASSERT_TRUE(tcp_client->write("hello"));
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Create TLS transport socket and install it in fake_upstream.
  Network::TransportSocketPtr ts =
      tls_context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(),
          std::vector<std::string>{"envoyalpn"}));

  // Synchronization object used to suspend execution
  // until dispatcher completes transport socket conversion.
  absl::Notification notification;

  // Execute transport socket conversion to TLS on the same thread where received data
  // is dispatched. Otherwise conversion may collide with data processing.
  fake_upstreams_[0]->dispatcher()->post([&]() {
    auto connection =
        dynamic_cast<Envoy::Network::ConnectionImpl*>(&fake_upstream_connection->connection());
    connection->transportSocket() = std::move(ts);
    connection->transportSocket()->setTransportSocketCallbacks(*connection);
    notification.Notify();
  });

  // Wait until the transport socket conversion completes.
  notification.WaitForNotification();

  // Send message which will trigger upstream starttls to use secure mode.
  ASSERT_TRUE(tcp_client->write("switch"));
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  // Send a message from upstream down through Envoy to tcp_client.
  ASSERT_TRUE(fake_upstream_connection->write("upstream"));
  tcp_client->waitForData("upstream");

  // Cleanup.
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  test_server_.reset();
}

INSTANTIATE_TEST_SUITE_P(StartTlsIntegrationTestSuite, StartTlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace Envoy
