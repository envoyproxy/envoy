/*
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "common/config/api_version.h"
#include "common/network/utility.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"
*/

#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/transport_sockets/raw_buffer/config.h"
#include "envoy/config/transport_socket/raw_buffer/v2/raw_buffer.pb.h"
#include "envoy/config/transport_socket/raw_buffer/v2/raw_buffer.pb.validate.h"
#include "common/network/connection_impl.h"


#include "test/integration/start_tls_integration_test.pb.h"
#include "test/integration/start_tls_integration_test.pb.validate.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
//#include "test/integration/utility.h"
#include "test/test_common/registry.h"
#include "gtest/gtest.h"


namespace Envoy {


class StartTlsSwitchFilter : public Network::Filter {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
};

// Network::ReadFilter
Network::FilterStatus StartTlsSwitchFilter::onNewConnection() { return Network::FilterStatus::Continue; }

Network::FilterStatus StartTlsSwitchFilter::onData(Buffer::Instance&, bool) {
  printf("Data Received onData!\n");
//  data.drain(data.length());
  printf("%s\n", read_callbacks_->connection().transportProtocol().c_str());
  if (read_callbacks_->connection().transportProtocol() ==
      Extensions::TransportSockets::TransportProtocolNames::get().StartTls) {
    // We run on top of the `STARTTLS` socket and can ask it to convert to SSL.
  printf("Switching to TLS!\n");
    read_callbacks_->connection().startSecureTransport();
  }
#if 0
#endif

  return Network::FilterStatus::Continue;
}

// Network::WriteFilter
Network::FilterStatus StartTlsSwitchFilter::onWrite(Buffer::Instance&, bool) {
  printf("Data Received onWrite!\n");
  //data.drain(data.length());
  return Network::FilterStatus::Continue;
}

/**
 * Config factory for StartTlsSwitchFilter.
 */
class StartTlsSwitchFilterConfigFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                         test::integration::start_tls::StartTlsFilterConfig> {
public:
  explicit StartTlsSwitchFilterConfigFactory(const std::string& name) :  FactoryBase(name) {}

  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::start_tls::StartTlsFilterConfig& ,
      Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<StartTlsSwitchFilter>());
    };
  }

/*
  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<StartTlsSwitchFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
*/

  std::string name() const override { return name_; }

private:
  const std::string name_;
};

#if 0
  /**
   * Add a network filter prior to existing filters.
   * @param config_helper helper object.
   * @param filter_yaml configuration snippet.
   */
  void addNetworkFilter(ConfigHelper& config_helper, const std::string& filter_yaml) {
    config_helper.addConfigModifier(
        [filter_yaml](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
          auto l = bootstrap.mutable_static_resources()->mutable_listeners(0);
          ASSERT_GT(l->filter_chains_size(), 0);

          auto* filter_chain = l->mutable_filter_chains(0);
          auto* filter_list_back = filter_chain->add_filters();
          TestUtility::loadFromYaml(filter_yaml, *filter_list_back);

          // Now move it to the front.
          for (int i = filter_chain->filters_size() - 1; i > 0; --i) {
            filter_chain->mutable_filters()->SwapElements(i, i - 1);
          }
        });
  }
#endif
  /**
   * Add a network filter prior to existing filters.
   * @param config_helper helper object.
   * @param filter_yaml configuration snippet.
   */

class StartTlsTest {
public:
  void addNetworkFilter(ConfigHelper& config_helper, const std::string& filter_yaml) {
    config_helper.addConfigModifier(
        [filter_yaml](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
          auto l = bootstrap.mutable_static_resources()->mutable_listeners(0);
          ASSERT_GT(l->filter_chains_size(), 0);

          auto* filter_chain = l->mutable_filter_chains(0);
          auto* filter_list_back = filter_chain->add_filters();
          TestUtility::loadFromYaml(filter_yaml, *filter_list_back);

          // Now move it to the front.
          for (int i = filter_chain->filters_size() - 1; i > 0; --i) {
            filter_chain->mutable_filters()->SwapElements(i, i - 1);
          }
        });
  }
  void addAuxiliaryFilter(ConfigHelper& config_helper) {
    addNetworkFilter(config_helper, R"EOF(
      name: startTle
      typed_config:
        "@type": type.googleapis.com/test.integration.start_tls.StartTlsFilterConfig
    )EOF");
    // double-check the filter was actually added
    config_helper.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ASSERT_EQ("startTle",
                bootstrap.static_resources().listeners(0).filter_chains(0).filters(0).name());
    });
  }
  StartTlsSwitchFilterConfigFactory config_factory_{"startTle"};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_config_factory_{config_factory_};
};


// TestConnection is derived from Connection class.
// Few methods are added to allow for switching transport socket, but keeping
// opened connection socket.
class TestConnection : public Network::ClientConnectionImpl {
public:
  TestConnection(Event::Dispatcher& dispatcher,
                       const Network::Address::InstanceConstSharedPtr& remote_address,
                       const Network::Address::InstanceConstSharedPtr& source_address,
                       Network::TransportSocketPtr&& transport_socket,
                       const Network::ConnectionSocket::OptionsSharedPtr& options):
  ClientConnectionImpl(dispatcher,
                       remote_address,
                       source_address,
                       std::move(transport_socket),
                       options){}

  void setTransportSocket(
                       Network::TransportSocketPtr&& transport_socket) {
  old_transport_socket_ = std::move(transport_socket_);
  transport_socket_ = std::move(transport_socket);
#if 0
  Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;

  // We never ask for both early close and read at the same time. If we are reading, we want to
  // consume all available data.
  file_event_ = socket_->ioHandle().createFileEvent(
      dispatcher_, [this](uint32_t events) -> void { onFileEvent(events); }, trigger,
      Event::FileReadyType::Read | Event::FileReadyType::Write);
#endif
  //onWriteReady();
  transport_socket_->setTransportSocketCallbacks(*this);

  connecting_ = true;
      file_event_->activate(Event::FileReadyType::Write);
}
  Network::TransportSocketPtr old_transport_socket_;
};
///////////////////////////////////////
class StartTlsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public BaseIntegrationTest,
                                public StartTlsTest {
public:
  StartTlsIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::startTlsConfig()) {
    enable_half_close_ = true;
}
  ~StartTlsIntegrationTest() {
  //delete client_write_buffer_;
  }
  void initialize() override;
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  Network::TransportSocketFactoryPtr cleartext_context_;
  MockWatermarkBuffer* client_write_buffer_{nullptr};
  ConnectionStatusCallbacks connect_callbacks_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::ClientConnectionPtr ssl_client_;

  Network::ClientConnectionPtr cleartext_client_;
};

void StartTlsIntegrationTest::initialize() {
  EXPECT_CALL(*mock_buffer_factory_, create_(_, _, _))
      .Times(1)
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
;
//printf("%s\n", ConfigHelper::startTlsConfig().c_str());
//    config_helper_.addRuntimeOverride("envoy.reloadable_features.new_tcp_connection_pool", "false");
//    config_helper_.addRuntimeOverride("envoy.reloadable_features.tls_use_io_handle_bio", "true");
  config_helper_.renameListener("tcp_proxy");
  addAuxiliaryFilter(config_helper_);
  BaseIntegrationTest::initialize();
    context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  payload_reader_ = std::make_shared<WaitForPayloadReader>(*dispatcher_);
}

TEST_P(StartTlsIntegrationTest, DISABLED_BasicCleartextConnect) {
    initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);

   // Make sure that messages are received upstream.
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // The transport socket has switched to TLS. Another write should fail.
  //ASSERT_TRUE(tcp_client->write("secondhello"));
  //tcp_client->waitForHalfClose();
  //sleep(1);
  //ASSERT_FALSE(tcp_client->connected());
  //ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  tcp_client->close();
}

TEST_P(StartTlsIntegrationTest, DISABLED_BasicRawConnect) {
    initialize();

    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
    
  //static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  auto factory = 
      (new Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory());
  context_ = 
  Network::TransportSocketFactoryPtr{
      factory->createTransportSocketFactory(
*(std::make_unique<envoy::config::transport_socket::raw_buffer::v2::RawBuffer>()), 
factory_context_ /**(std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>())context_manager_*/)};
  //                                                                  *client_stats_store)};
    cleartext_client_ = dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        context_->createTransportSocket(
            // nullptr
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(),
                std::vector<std::string>{"envoyalpn"})),
        nullptr);
    cleartext_client_->addConnectionCallbacks(connect_callbacks_);
  cleartext_client_->enableHalfClose(true);
    cleartext_client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);
    
  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  cleartext_client_->write(buffer, false);
  while (client_write_buffer_->bytes_drained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
}

TEST_P(StartTlsIntegrationTest, DISABLED_BasicSslConnect) {
    initialize();


    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
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
  ssl_client_->enableHalfClose(true);
    ssl_client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);
    
  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytes_drained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
/*
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
*/
  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_TRUE(connect_callbacks_.closed());
}

TEST_P(StartTlsIntegrationTest, SwitchTest) {
    initialize();
    
  //static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  auto config = 
std::make_unique<envoy::config::transport_socket::raw_buffer::v2::RawBuffer>();
  auto factory = std::make_unique<Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory>();
  cleartext_context_ = 
  Network::TransportSocketFactoryPtr{
      factory->createTransportSocketFactory(
      *config,
factory_context_ /**(std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>())context_manager_*/)};
  //                                                                  *client_stats_store)};
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
    std::unique_ptr<TestConnection> conn = std::make_unique<TestConnection>(*dispatcher_, 
        address, Network::Address::InstanceConstSharedPtr(),
        cleartext_context_->createTransportSocket(
            // nullptr
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(),
                std::vector<std::string>())), nullptr);
/*
    cleartext_client_ = dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        cleartext_context_->createTransportSocket(
            // nullptr
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(),
                std::vector<std::string>{"envoyalpn"})),
        nullptr);
*/

  conn->enableHalfClose(true);
   conn->addConnectionCallbacks(connect_callbacks_);
  conn->connect();
/*
    ssl_client_ = dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        context_->createTransportSocket(
            // nullptr
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(),
                std::vector<std::string>{"envoyalpn"})),
        nullptr);
  ssl_client_->enableHalfClose(true);

// At this moment we have two not connected clients.
    cleartext_client_->addConnectionCallbacks(connect_callbacks_);
    cleartext_client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
*/
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);
    
  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  conn->write(buffer, false);
  while (client_write_buffer_->bytes_drained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Switch to TLS happened after the first read..
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
  conn->setTransportSocket(
        context_->createTransportSocket(
            // nullptr
            std::make_shared<Network::TransportSocketOptionsImpl>(
                absl::string_view(""), std::vector<std::string>(),
                std::vector<std::string>{"envoyalpn"})));
    //conn->connect();
    connect_callbacks_.reset();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  buffer.add("hurra");
  //conn->connect();
  conn->write(buffer, true);
  while (client_write_buffer_->bytes_drained() != 10) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(10));
#if 0
    ssl_client_->addConnectionCallbacks(connect_callbacks_);
    cleartext_client_->reset();
    ssl_client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
/*
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);
  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  */  
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytes_drained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
#endif
/*
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
*/
#if 0
  Buffer::OwnedImpl empty_buffer;
  conn->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  conn->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_TRUE(connect_callbacks_.closed());
#endif
  conn->close(Network::ConnectionCloseType::FlushWrite);
}

INSTANTIATE_TEST_SUITE_P(StartTlsIntegrationTestSuite, StartTlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} //namespace Envoy
