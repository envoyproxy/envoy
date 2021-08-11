#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/basic_resource_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "source/extensions/filters/listener/proxy_protocol/proxy_protocol.h"
#include "source/server/connection_handler_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ProxyProtocol {
namespace {

/**
 * Regression tests for testing that the PROXY protocol listener filter can correctly read
 * what the PROXY protocol util functions generate
 */
class ProxyProtocolRegressionTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public Network::ListenerConfig,
                                    public Network::FilterChainManager,
                                    protected Logger::Loggable<Logger::Id::main> {
public:
  ProxyProtocolRegressionTest()
      : api_(Api::createApiForTest(stats_store_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        socket_(std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
            Network::Test::getCanonicalLoopbackAddress(GetParam()))),
        connection_handler_(new Server::ConnectionHandlerImpl(*dispatcher_, absl::nullopt)),
        name_("proxy"), filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()),
        init_manager_(nullptr) {
    EXPECT_CALL(socket_factory_, socketType()).WillOnce(Return(Network::Socket::Type::Stream));
    EXPECT_CALL(socket_factory_, localAddress())
        .WillOnce(ReturnRef(socket_->addressProvider().localAddress()));
    EXPECT_CALL(socket_factory_, getListenSocket(_)).WillOnce(Return(socket_));
    connection_handler_->addListener(absl::nullopt, *this);
    conn_ = dispatcher_->createClientConnection(socket_->addressProvider().localAddress(),
                                                Network::Address::InstanceConstSharedPtr(),
                                                Network::Test::createRawBufferSocket(), nullptr);
    conn_->addConnectionCallbacks(connection_callbacks_);
  }

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return *this; }
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::ListenSocketFactory& listenSocketFactory() override { return socket_factory_; }
  bool bindToPort() override { return true; }
  bool handOffRestoredDestinationConnections() const override { return false; }
  uint32_t perConnectionBufferLimitBytes() const override { return 0; }
  std::chrono::milliseconds listenerFiltersTimeout() const override { return {}; }
  bool continueOnListenerFiltersTimeout() const override { return false; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  const std::string& name() const override { return name_; }
  Network::UdpListenerConfigOptRef udpListenerConfig() override {
    return Network::UdpListenerConfigOptRef();
  }
  ResourceLimit& openConnections() override { return open_connections_; }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return envoy::config::core::v3::UNSPECIFIED;
  }
  Network::ConnectionBalancer& connectionBalancer() override { return connection_balancer_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return empty_access_logs_;
  }
  uint32_t tcpBacklogSize() const override { return ENVOY_TCP_BACKLOG_SIZE; }
  Init::Manager& initManager() override { return *init_manager_; }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  void connect(bool read = true) {
    int expected_callbacks = 2;
    auto maybeExitDispatcher = [&]() -> void {
      expected_callbacks--;
      if (expected_callbacks == 0) {
        dispatcher_->exit();
      }
    };

    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](Network::ListenerFilterManager& filter_manager) -> bool {
          filter_manager.addAcceptFilter(
              nullptr,
              std::make_unique<ListenerFilters::ProxyProtocol::Filter>(
                  std::make_shared<ListenerFilters::ProxyProtocol::Config>(
                      listenerScope(),
                      envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol())));
          maybeExitDispatcher();
          return true;
        }));
    conn_->connect();
    if (read) {
      read_filter_ = std::make_shared<NiceMock<Network::MockReadFilter>>();
      EXPECT_CALL(factory_, createNetworkFilterChain(_, _))
          .WillOnce(Invoke([&](Network::Connection& connection,
                               const std::vector<Network::FilterFactoryCb>&) -> bool {
            server_connection_ = &connection;
            connection.addConnectionCallbacks(server_callbacks_);
            connection.addReadFilter(read_filter_);
            return true;
          }));
    }
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { maybeExitDispatcher(); }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void write(const uint8_t* s, ssize_t l) {
    Buffer::OwnedImpl buf(s, l);
    conn_->write(buf, false);
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf, false);
  }

  void expectData(std::string expected) {
    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> Network::FilterStatus {
          EXPECT_EQ(buffer.toString(), expected);
          buffer.drain(expected.length());
          dispatcher_->exit();
          return Network::FilterStatus::Continue;
        }));

    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    conn_->close(Network::ConnectionCloseType::NoFlush);

    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Network::TcpListenSocket> socket_;
  Network::MockListenSocketFactory socket_factory_;
  Network::NopConnectionBalancerImpl connection_balancer_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::MockFilterChainFactory factory_;
  Network::ClientConnectionPtr conn_;
  NiceMock<Network::MockConnectionCallbacks> connection_callbacks_;
  BasicResourceLimitImpl open_connections_;
  Network::Connection* server_connection_;
  Network::MockConnectionCallbacks server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  std::string name_;
  const Network::FilterChainSharedPtr filter_chain_;
  const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
  std::unique_ptr<Init::Manager> init_manager_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolRegressionTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ProxyProtocolRegressionTest, V1Basic) {
  std::string source_addr;
  Buffer::OwnedImpl buff{};
  if (GetParam() == Network::Address::IpVersion::v4) {
    source_addr = "202.168.0.13";
    generateV1Header(source_addr, "174.2.2.222", 52000, 80, Network::Address::IpVersion::v4, buff);
  } else {
    source_addr = "1:2:3::4";
    generateV1Header(source_addr, "5:6::7:8", 62000, 8000, Network::Address::IpVersion::v6, buff);
  }
  connect();

  write(buff.toString() + "more data");

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            source_addr);
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolRegressionTest, V2Basic) {
  std::string source_addr;
  Buffer::OwnedImpl buff{};
  if (GetParam() == Network::Address::IpVersion::v4) {
    source_addr = "202.168.0.13";
    generateV2Header(source_addr, "174.2.2.222", 52000, 80, Network::Address::IpVersion::v4, buff);
  } else {
    source_addr = "1:2:3::4";
    generateV2Header(source_addr, "5:6::7:8", 62000, 8000, Network::Address::IpVersion::v6, buff);
  }
  connect();

  write(buff.toString() + "more data");

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            source_addr);
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolRegressionTest, V2LocalConnection) {
  Buffer::OwnedImpl buff{};
  generateV2LocalHeader(buff);
  connect();

  write(buff.toString() + "more data");

  expectData("more data");

  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "127.0.0.1");
  } else {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "::1");
  }
  EXPECT_FALSE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

} // namespace
} // namespace ProxyProtocol
} // namespace Common
} // namespace Extensions
} // namespace Envoy
