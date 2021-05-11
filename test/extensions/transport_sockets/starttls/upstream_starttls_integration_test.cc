#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.validate.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "common/network/connection_impl.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/transport_sockets/raw_buffer/config.h"
#include "extensions/transport_sockets/starttls/starttls_socket.h"
#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/config/utility.h"
#include "test/extensions/transport_sockets/starttls/starttls_integration_test.pb.h"
#include "test/extensions/transport_sockets/starttls/starttls_integration_test.pb.validate.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {

class TerminalServerTlsFilter : public Network::ReadFilter {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& buf, bool) override {
    auto message = buf.toString();
    if (message != "usetls") {
      // Just echo anything other than the 'usetls' command.
      read_callbacks_->connection().write(buf, false);
    } else {
      read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
        // Wait until 6 bytes long "usetls" has been sent.
        if (bytes >= 6) {
          read_callbacks_->connection().startSecureTransport();
          // Unsubscribe the callback.
          // Switch to tls has been completed.
          return false;
        }
        return true;
      });

      buf.drain(buf.length());
      buf.add("switch");
      read_callbacks_->connection().write(buf, false);
    }

    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
};

// Simple filter for test purposes. This filter will be injected into the filter chain during
// tests. The filter reacts only to few keywords. If received payload does not contain
// allowed keyword, filter will stop iteration.
// The filter will be configured to sit on top of tcp_proxy and use start-tls transport socket.
// If it receives a data which is not known keyword it means that transport socket has not been
// successfully converted to use TLS and filter receives either encrypted data or TLS handshake
// messages.
class StartTlsSwitchFilter : public Network::Filter {
public:
  ~StartTlsSwitchFilter() override {
    if (upstream_connection_) {
      upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
    }
  }

  void upstreamWrite(Buffer::Instance& data, bool end_stream);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  static std::shared_ptr<StartTlsSwitchFilter>
  newInstance(Upstream::ClusterManager& cluster_manager) {
    auto p = std::shared_ptr<StartTlsSwitchFilter>(new StartTlsSwitchFilter(cluster_manager));
    p->self_ = p;
    return p;
  }

  struct UpstreamReadFilter : public Network::ReadFilter {

    UpstreamReadFilter(std::weak_ptr<StartTlsSwitchFilter> parent) : parent_(parent) {}

    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      ENVOY_LOG_MISC(debug, "EXTRA READ FILTER RECEIVED SOME DATA");
      if (auto parent = parent_.lock()) {
        parent->upstreamWrite(data, end_stream);
        parent->onWrite(data, end_stream);
        return Network::FilterStatus::Continue;
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
  auto c = cluster_manager_.getThreadLocalCluster("cluster_0" /*"dummy_cluster"*/);
  auto h = c->loadBalancer().chooseHost(nullptr);
  upstream_connection_ =
      h->createConnection(read_callbacks_->connection().dispatcher(), nullptr, nullptr).connection_;
  upstream_connection_->addReadFilter(std::make_shared<UpstreamReadFilter>(self_));
  upstream_connection_->connect();
  return Network::FilterStatus::Continue;
}

Network::FilterStatus StartTlsSwitchFilter::onData(Buffer::Instance& data, bool end_stream) {

  ENVOY_LOG_MISC(debug, "RECEIVED SOME DATA");
  if (end_stream) {
    upstream_connection_->close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }

  upstreamWrite(data, end_stream);
  upstream_connection_->write(data, end_stream);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus StartTlsSwitchFilter::onWrite(Buffer::Instance& buf, bool end_stream) {
  read_callbacks_->connection().write(buf, end_stream);
  return Network::FilterStatus::Continue;
}

void StartTlsSwitchFilter::upstreamWrite(Buffer::Instance& buf, bool /*end_stream*/) {
  const std::string message = buf.toString();
  ENVOY_LOG_MISC(debug, "RECEIVED STARTTLS COMMAND {}", message.c_str());
  if (message == "switch") {
    // Start the upstream secure transport immediately since we clearly have all the bytes
    ASSERT_TRUE(upstream_connection_->startSecureTransport());
#if 0
    read_callbacks_->connection().addBytesSentCallback([=](uint64_t bytes) -> bool {
      // Wait until 6 bytes long "switch" has been sent.
      if (bytes >= 6) {
        read_callbacks_->connection().startSecureTransport();
        // Unsubscribe the callback.
        // Switch to tls has been completed.
        return false;
      }
      return true;
    });
#endif
  }
  // Finally just forward the data downstream.
  // read_callbacks_->connection().write(buf, end_stream);
}

// Config factory for StartTlsSwitchFilter.
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

// ClientTestConnection is used for simulating a client
// which initiates a connection to Envoy in clear-text and then switches to TLS
// without closing the socket.
class ClientTestConnection : public Network::ClientConnectionImpl {
public:
  ClientTestConnection(Event::Dispatcher& dispatcher,
                       const Network::Address::InstanceConstSharedPtr& remote_address,
                       const Network::Address::InstanceConstSharedPtr& source_address,
                       Network::TransportSocketPtr&& transport_socket,
                       const Network::ConnectionSocket::OptionsSharedPtr& options)
      : ClientConnectionImpl(dispatcher, remote_address, source_address,
                             std::move(transport_socket), options) {}

  void setTransportSocket(Network::TransportSocketPtr&& transport_socket) {
    transport_socket_ = std::move(transport_socket);
    transport_socket_->setTransportSocketCallbacks(*this);

    // Reset connection's state machine.
    connecting_ = true;

    // Issue event which will trigger TLS handshake.
    ioHandle().activateFileEvents(Event::FileReadyType::Write);
  }
};

// Fixture class for integration tests.
class StartTlsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public BaseIntegrationTest {
public:
  StartTlsIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig()),
        stream_info_(timeSystem(), nullptr) {}
  void initialize() override;
  void addStartTlsSwitchFilter(ConfigHelper& config_helper);

  // Contexts needed by raw buffer and tls transport sockets.
  Network::TransportSocketFactoryPtr cleartext_context_;

  MockWatermarkBuffer* client_write_buffer_{nullptr};
  ConnectionStatusCallbacks connect_callbacks_;

  // Config factory for StartTlsSwitchFilter.
  StartTlsSwitchFilterConfigFactory config_factory_{"startTls"};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_config_factory_{config_factory_};

  std::unique_ptr<ClientTestConnection> conn_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  Network::ListenerPtr listener_;
  Network::MockTcpListenerCallbacks listener_callbacks_;
  Network::ServerConnectionPtr server_connection_;

  // Technically unused.
  StreamInfo::StreamInfoImpl stream_info_;
  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::TransportSocketFactoryPtr tls_context_;
};

void StartTlsIntegrationTest::initialize() {
  EXPECT_CALL(*mock_buffer_factory_, create_(_, _, _))
      // Connection constructor will first create write buffer.
      // Test tracks how many bytes are sent.
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer_ =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        ON_CALL(*client_write_buffer_, move(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer_, drain(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer_;
      }))
      // Connection constructor will also create read buffer, but the test does
      // not track received bytes.
      .WillRepeatedly(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                                 std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));

  config_helper_.renameListener("tcp_proxy");
  // addStartTlsSwitchFilter(config_helper_);

  // Add transport socket to cluster_0
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

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()->mutable_listeners(0)->add_filter_chains();
    config_helper_.addNetworkFilter(R"EOF(
      name: startTls
      typed_config:
        "@type": type.googleapis.com/test.integration.starttls.StartTlsFilterConfig
    )EOF");
  });
  // Setup factories and contexts for upstream clear-text raw buffer transport socket.
  auto config = std::make_unique<envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer>();

  auto factory =
      std::make_unique<Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory>();
  cleartext_context_ = Network::TransportSocketFactoryPtr{
      factory->createTransportSocketFactory(*config, factory_context_)};
  payload_reader_ = std::make_shared<WaitForPayloadReader>(*dispatcher_);

  // Setup factories and contexts for tls transport socket.
  tls_context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  //  tls_context_ = Ssl::createClientSslTransportSocketFactory({}, *tls_context_manager_, *api_);
  //  START
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  // ServerSslTransportOptions options;
  // initializeUpstreamTlsContextConfig({}, tls_context);

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

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_plain), tls_context);
  // END CONFIG

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx, api()).WillByDefault(testing::ReturnRef(*api_));
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      tls_context, mock_factory_ctx);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  tls_context_ = Network::TransportSocketFactoryPtr{
      new Extensions::TransportSockets::Tls::ServerSslSocketFactory(
          std::move(cfg), *tls_context_manager_, *client_stats_store, {})};
  //  END

#if 0
  auto socket = std::make_shared<Network::TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr, true);
#if 0
  // Prepare for the server side listener
  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        auto server_tls_context_ = Ssl::createUpstreamSslContext(*tls_context_manager_, *api_);

        auto startTlsTransportSocket =
            Extensions::TransportSockets::StartTls::StartTlsSocketFactory(move(cleartext_context_),
                                                                          move(server_tls_context_))
                .createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
                    absl::string_view(""), std::vector<std::string>(), std::vector<std::string>()));

        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), move(startTlsTransportSocket), stream_info_);
        server_connection_->addReadFilter(std::make_shared<TerminalServerTlsFilter>());
      }));

  listener_ =
      dispatcher_->createListener(socket, listener_callbacks_, true, ENVOY_TCP_BACKLOG_SIZE);

#endif
  // Add a start_tls cluster
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    *bootstrap.mutable_static_resources()->add_clusters() = ConfigHelper::buildStartTlsCluster(
        socket->addressProvider().localAddress()->ip()->addressAsString(),
        socket->addressProvider().localAddress()->ip()->port());
  });
#endif
  BaseIntegrationTest::initialize();

  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));

  conn_ = std::make_unique<ClientTestConnection>(
      *dispatcher_, address, Network::Address::InstanceConstSharedPtr(),
      cleartext_context_->createTransportSocket(
          std::make_shared<Network::TransportSocketOptionsImpl>(
              absl::string_view(""), std::vector<std::string>(), std::vector<std::string>())),
      nullptr);
  conn_->enableHalfClose(true);
  conn_->addConnectionCallbacks(connect_callbacks_);
  conn_->addReadFilter(payload_reader_);
}

// Method adds StartTlsSwitchFilter into the filter chain.
// The filter is required to instruct StartTls transport
// socket to start using Tls.
void StartTlsIntegrationTest::addStartTlsSwitchFilter(ConfigHelper& config_helper) {
  config_helper.addNetworkFilter(R"EOF(
      name: startTls
      typed_config:
        "@type": type.googleapis.com/test.integration.starttls.StartTlsFilterConfig
    )EOF");
  // double-check the filter was actually added
  config_helper.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ASSERT_EQ("startTls",
              bootstrap.static_resources().listeners(0).filter_chains(0).filters(0).name());
  });
}
//#############################################################################
TEST_P(StartTlsIntegrationTest, SwitchToTls) {
  initialize();

  // Open clear-text connection.
  conn_->connect();

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);

  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  conn_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  Network::TransportSocketPtr ts =
      tls_context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(),
          std::vector<std::string>{"envoyalpn"}));
  fake_upstream_connection->setTransportSocket(std::move(ts));
  // tls_context_.reset();
#if 0
      tls_context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(),
          std::vector<std::string>{"envoyalpn"})));
#endif
  // dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // Switch to tls.
  buffer.add("switch");
  conn_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 11) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

#if 0
  // Wait for confirmation
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // Send a message to upstream cluster
  buffer.add("hello");
  conn_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
#endif
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  // dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  const std::string data("upstream");
  ASSERT_TRUE(fake_upstream_connection->write(data, false));

  // Wait for confirmation
  payload_reader_->set_data_to_wait_for("upstream");
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  // ASSERT_TRUE(fake_upstream_connection->close());
  //  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  test_server_.reset();
  // sleep(1); - is working
  // server_connection_->close(Network::ConnectionCloseType::FlushWrite);
}

// Test creates a clear-text connection from a client to Envoy and sends several messages.
// Then a special message is sent, which causes StartTlsSwitchFilter to
// instruct StartTls transport socket to start using tls for both the upstream and
// downstream. Connections. The Client connection starts using tls, performs tls handshake
// and a message is sent over tls. start-tls transport socket de-crypts the messages and
// forwards them upstream over yet another start-tls transport.
TEST_P(StartTlsIntegrationTest, DISABLED_SwitchToTlsFromClient) {
  initialize();

  // Open clear-text connection.
  conn_->connect();

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Send a message to switch to tls on the receiver side.
  // StartTlsSwitchFilter will switch transport socket on the
  // upstream side upon receiving "switch" message and forward
  // back the message "switch" to this client.
  payload_reader_->set_data_to_wait_for("switch");
  Buffer::OwnedImpl buffer;
  buffer.add("usetls");
  conn_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 6) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ASSERT_TRUE(payload_reader_->waitForLength(6, std::chrono::milliseconds(100000)));

  // Make sure we received the 'switch' command from the upstream.
  ASSERT_EQ("switch", payload_reader_->data());
  payload_reader_->clearData();

  // Without closing the connection, switch to tls.
  conn_->setTransportSocket(
      tls_context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(), std::vector<std::string>())));
  connect_callbacks_.reset();

  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // // Send few messages over the encrypted connection.
  buffer.add("hola");
  conn_->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 10) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Make sure we get our echo back
  ASSERT_TRUE(payload_reader_->waitForLength(4, std::chrono::milliseconds(100000)));
  ASSERT_EQ("hola", payload_reader_->data());
  payload_reader_->clearData();

  conn_->close(Network::ConnectionCloseType::FlushWrite);
  server_connection_->close(Network::ConnectionCloseType::FlushWrite);
}

INSTANTIATE_TEST_SUITE_P(StartTlsIntegrationTestSuite, StartTlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace Envoy
