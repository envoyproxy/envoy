#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

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
      : BaseIntegrationTest(GetParam(), ConfigHelper::TCP_PROXY_CONFIG) {
    enable_half_close_ = true;
  }

  ~ListenerFilterIntegrationTest() override {
    // test_server_.reset();
    // fake_upstreams_.clear();
  }
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
    config_helper_.renameListener("tcp_proxy");
    std::string tls_inspector_config = ConfigHelper::DEFAULT_TLS_INSPECTOR_LISTENER_FILTER;
    if (listener_filter_disabled.has_value()) {
      tls_inspector_config = appendMatcher(tls_inspector_config, listener_filter_disabled.value());
    }
    config_helper_.addListenerFilter(tls_inspector_config);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain = bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto *alpn = filter_chain->mutable_filter_chain_match()->add_application_protocols();
      *alpn = "envoyalpn";
      });
    config_helper_.addSslConfig();
    BaseIntegrationTest::initialize();

    context_manager_ =
        std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
    payload_reader_.reset(new WaitForPayloadReader(*dispatcher_));
  }

  void setupConnections(bool listener_filter_disabled) {
    initializeWithListenerFilter(listener_filter_disabled);

    // Set up the mock buffer factory so the newly created SSL client will have a mock write
    // buffer. This allows us to track the bytes actually written to the socket.

    EXPECT_CALL(*mock_buffer_factory_, create_(_, _))
        .Times(1)
        .WillOnce(Invoke([&](std::function<void()> below_low,
                             std::function<void()> above_high) -> Buffer::Instance* {
          client_write_buffer_ = new NiceMock<MockWatermarkBuffer>(below_low, above_high);
          ON_CALL(*client_write_buffer_, move(_))
              .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
          ON_CALL(*client_write_buffer_, drain(_))
              .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
          return client_write_buffer_;
        }));
    // Set up the SSL client.
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
    ssl_client_ =
        dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                            context_->createTransportSocket(
                                              //nullptr
                                              std::make_shared<Network::TransportSocketOptionsImpl>(
                                                absl::string_view(""),
                                                std::vector<std::string>(),
                                                std::vector<std::string>{"envoyalpn"}
                                              )
                                              ), nullptr);

    // Perform the SSL handshake. Loopback is whitelisted in tcp_proxy.json for the ssl_auth
    // filter so there will be no pause waiting on auth data.
    ssl_client_->addConnectionCallbacks(connect_callbacks_);
    ssl_client_->enableHalfClose(true);
    ssl_client_->addReadFilter(payload_reader_);
    ssl_client_->connect();
    while (!connect_callbacks_.connected()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }

    AssertionResult result = fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
  }

    void setupFailureConnections(bool listener_filter_disabled) {
    initializeWithListenerFilter(listener_filter_disabled);

    // Set up the mock buffer factory so the newly created SSL client will have a mock write
    // buffer. This allows us to track the bytes actually written to the socket.

    EXPECT_CALL(*mock_buffer_factory_, create_(_, _))
        .Times(1)
        .WillOnce(Invoke([&](std::function<void()> below_low,
                             std::function<void()> above_high) -> Buffer::Instance* {
          client_write_buffer_ = new NiceMock<MockWatermarkBuffer>(below_low, above_high);
          ON_CALL(*client_write_buffer_, move(_))
              .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
          ON_CALL(*client_write_buffer_, drain(_))
              .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
          return client_write_buffer_;
        }));
    // Set up the SSL client.
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
    context_ = Ssl::createClientSslTransportSocketFactory({}, *context_manager_, *api_);
    ssl_client_ =
        dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                            context_->createTransportSocket(
                                              //nullptr
                                              std::make_shared<Network::TransportSocketOptionsImpl>(
                                                absl::string_view(""),
                                                std::vector<std::string>(),
                                                std::vector<std::string>{"envoyalpn"}
                                              )
                                              ), nullptr);

    // Perform the SSL handshake. Loopback is whitelisted in tcp_proxy.json for the ssl_auth
    // filter so there will be no pause waiting on auth data.
    ssl_client_->addConnectionCallbacks(connect_callbacks_);
    ssl_client_->enableHalfClose(true);
    ssl_client_->addReadFilter(payload_reader_);
    ssl_client_->connect();
    while (!connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  MockWatermarkBuffer* client_write_buffer_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::ClientConnectionPtr ssl_client_;
  FakeRawConnectionPtr fake_upstream_connection_;
};

// Each listener filter is enabled by default.
TEST_P(ListenerFilterIntegrationTest, AllListenerFiltersAreEnabledByDefault) {
  setupConnections(false);
  const std::string data_to_send_upstream = "hello";
  const std::string data_to_send_downstream = "world";

  Buffer::OwnedImpl buffer(data_to_send_upstream);
  ssl_client_->write(buffer, false);
  while (client_write_buffer_->bytes_drained() != data_to_send_upstream.size()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection_->waitForData(data_to_send_upstream.size()));

  // Now send data downstream and make sure it arrives.
  ASSERT_TRUE(fake_upstream_connection_->write(data_to_send_downstream));
  payload_reader_->set_data_to_wait_for(data_to_send_downstream);
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);

  // Clean up.
  Buffer::OwnedImpl empty_buffer;
  ssl_client_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(fake_upstream_connection_->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection_->write("", true));
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ssl_client_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(payload_reader_->readLastByte());
  EXPECT_TRUE(connect_callbacks_.closed());
}

// A listener filter could be disabled. See AllListenerFiltersAreEnabledByDefault for the behavior
// of filter is enabled.
TEST_P(ListenerFilterIntegrationTest, DisabledTlsInspectorFailsFilterChainFind) {
  setupFailureConnections(true);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ListenerFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
} // namespace
} // namespace Envoy
