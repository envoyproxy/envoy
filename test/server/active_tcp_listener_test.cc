#include <memory>

#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/utility.h"
#include "source/server/active_tcp_listener.h"

#include "test/mocks/common.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace {

class MockTcpConnectionHandler : public Network::TcpConnectionHandler,
                                 public Network::MockConnectionHandler {
public:
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::BalancedConnectionHandlerOptRef, getBalancedHandlerByTag,
              (uint64_t listener_tag));
  MOCK_METHOD(Network::BalancedConnectionHandlerOptRef, getBalancedHandlerByAddress,
              (const Network::Address::Instance& address));
};

class ActiveTcpListenerTest : public testing::Test, protected Logger::Loggable<Logger::Id::main> {
public:
  ActiveTcpListenerTest() {
    EXPECT_CALL(conn_handler_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(conn_handler_, numConnections()).Times(testing::AnyNumber());
    EXPECT_CALL(conn_handler_, statPrefix()).WillRepeatedly(ReturnRef(listener_stat_prefix_));
    listener_filter_matcher_ = std::make_shared<NiceMock<Network::MockListenerFilterMatcher>>();
  }

  std::string listener_stat_prefix_{"listener_stat_prefix"};
  std::shared_ptr<Network::MockListenSocketFactory> socket_factory_{
      std::make_shared<Network::MockListenSocketFactory>()};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  BasicResourceLimitImpl resource_limit_;
  NiceMock<MockTcpConnectionHandler> conn_handler_;
  Network::MockListener* generic_listener_;
  Network::MockListenerConfig listener_config_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> filter_chain_factory_;
  std::shared_ptr<Network::MockFilterChain> filter_chain_;
  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;
};

TEST_F(ActiveTcpListenerTest, PopulateSNIWhenActiveTcpSocketTimeout) {
  NiceMock<Network::MockConnectionBalancer> balancer;
  EXPECT_CALL(listener_config_, connectionBalancer()).WillRepeatedly(ReturnRef(balancer));
  EXPECT_CALL(listener_config_, listenerScope).Times(testing::AnyNumber());
  EXPECT_CALL(listener_config_, listenerFiltersTimeout())
      .WillOnce(Return(std::chrono::milliseconds(1000)));
  EXPECT_CALL(listener_config_, continueOnListenerFiltersTimeout());
  EXPECT_CALL(listener_config_, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));

  auto listener = std::make_unique<NiceMock<Network::MockListener>>();
  EXPECT_CALL(*listener, onDestroy());

  auto* test_filter = new NiceMock<Network::MockListenerFilter>();
  EXPECT_CALL(*test_filter, destroy_());
  EXPECT_CALL(listener_config_, filterChainFactory())
      .WillRepeatedly(ReturnRef(filter_chain_factory_));

  // add a filter to stop the filter iteration.
  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));

  auto active_listener =
      std::make_unique<ActiveTcpListener>(conn_handler_, std::move(listener), listener_config_);

  absl::string_view server_name = "envoy.io";
  auto accepted_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  accepted_socket->connection_info_provider_->setRequestedServerName(server_name);

  // fake the socket is open.
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle));
  EXPECT_CALL(io_handle, isOpen()).WillOnce(Return(true));

  EXPECT_CALL(balancer, pickTargetHandler(_))
      .WillOnce(testing::DoAll(
          testing::WithArg<0>(Invoke([](auto& target) { target.incNumConnections(); })),
          ReturnRef(*active_listener)));

  // calling the onAcceptWorker() to create the ActiveTcpSocket.
  active_listener->onAcceptWorker(std::move(accepted_socket), false, false);
  // get the ActiveTcpSocket pointer before unlink() removed from the link-list.
  ActiveTcpSocket* tcp_socket = active_listener->sockets().front().get();
  // trigger the onTimeout event manually, since the timer is fake.
  active_listener->sockets().front()->onTimeout();

  EXPECT_EQ(server_name,
            tcp_socket->stream_info_->downstreamAddressProvider().requestedServerName());
}

// Verify that the server connection with recovered address is rebalanced at redirected listener.
TEST_F(ActiveTcpListenerTest, RedirectedRebalancer) {
  NiceMock<Network::MockListenerConfig> listener_config1;
  NiceMock<Network::MockConnectionBalancer> balancer1;
  EXPECT_CALL(balancer1, registerHandler(_));
  EXPECT_CALL(balancer1, unregisterHandler(_));

  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  EXPECT_CALL(listener_config1, connectionBalancer()).WillRepeatedly(ReturnRef(balancer1));
  EXPECT_CALL(listener_config1, listenerScope).Times(testing::AnyNumber());
  EXPECT_CALL(listener_config1, listenerFiltersTimeout());
  EXPECT_CALL(listener_config1, continueOnListenerFiltersTimeout());
  EXPECT_CALL(listener_config1, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
  EXPECT_CALL(listener_config1, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
  EXPECT_CALL(listener_config1, handOffRestoredDestinationConnections())
      .WillRepeatedly(Return(true));

  auto mock_listener_will_be_moved1 = std::make_unique<Network::MockListener>();
  auto& listener1 = *mock_listener_will_be_moved1;
  auto active_listener1 = std::make_unique<ActiveTcpListener>(
      conn_handler_, std::move(mock_listener_will_be_moved1), listener_config1);

  NiceMock<Network::MockListenerConfig> listener_config2;
  Network::MockConnectionBalancer balancer2;
  EXPECT_CALL(balancer2, registerHandler(_));
  EXPECT_CALL(balancer2, unregisterHandler(_));

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(alt_address));
  EXPECT_CALL(listener_config2, listenerFiltersTimeout());
  EXPECT_CALL(listener_config2, connectionBalancer()).WillRepeatedly(ReturnRef(balancer2));
  EXPECT_CALL(listener_config2, listenerScope).Times(testing::AnyNumber());
  EXPECT_CALL(listener_config2, handOffRestoredDestinationConnections())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(listener_config2, continueOnListenerFiltersTimeout());
  EXPECT_CALL(listener_config2, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
  EXPECT_CALL(listener_config2, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
  auto mock_listener_will_be_moved2 = std::make_unique<Network::MockListener>();
  auto& listener2 = *mock_listener_will_be_moved2;
  auto active_listener2 = std::make_shared<ActiveTcpListener>(
      conn_handler_, std::move(mock_listener_will_be_moved2), listener_config2);

  auto* test_filter = new NiceMock<Network::MockListenerFilter>();
  EXPECT_CALL(*test_filter, destroy_());
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  bool redirected = false;

  // 1. Listener1 re-balance. Set the balance target to the the active listener itself.
  EXPECT_CALL(balancer1, pickTargetHandler(_))
      .WillOnce(testing::DoAll(
          testing::WithArg<0>(Invoke([](auto& target) { target.incNumConnections(); })),
          ReturnRef(*active_listener1)));

  EXPECT_CALL(listener_config1, filterChainFactory())
      .WillRepeatedly(ReturnRef(filter_chain_factory_));

  // Listener1 has a listener filter in the listener filter chain.
  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        if (!redirected) {
          manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{test_filter});
          redirected = true;
        }
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // Verify that listener1 hands off the connection by not creating network filter chain.
  EXPECT_CALL(manager_, findFilterChain(_)).Times(0);

  // 2. Redirect to Listener2.
  EXPECT_CALL(conn_handler_, getBalancedHandlerByAddress(_))
      .WillOnce(Return(Network::BalancedConnectionHandlerOptRef(*active_listener2)));

  // 3. Listener2 re-balance. Set the balance target to the the active listener itself.
  EXPECT_CALL(balancer2, pickTargetHandler(_))
      .WillOnce(testing::DoAll(
          testing::WithArg<0>(Invoke([](auto& target) { target.incNumConnections(); })),
          ReturnRef(*active_listener2)));

  auto filter_factory_callback = std::make_shared<std::vector<Network::FilterFactoryCb>>();
  auto transport_socket_factory = Network::Test::createRawBufferSocketFactory();
  filter_chain_ = std::make_shared<NiceMock<Network::MockFilterChain>>();

  EXPECT_CALL(conn_handler_, incNumConnections());
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  EXPECT_CALL(*filter_chain_, transportSocketFactory)
      .WillOnce(testing::ReturnRef(*transport_socket_factory));
  EXPECT_CALL(*filter_chain_, networkFilterFactories).WillOnce(ReturnRef(*filter_factory_callback));
  EXPECT_CALL(listener_config2, filterChainFactory())
      .WillRepeatedly(ReturnRef(filter_chain_factory_));

  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(filter_chain_factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  active_listener1->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  // Verify per-listener connection stats.
  EXPECT_EQ(1UL, conn_handler_.numConnections());

  EXPECT_CALL(conn_handler_, decNumConnections());
  connection->close(Network::ConnectionCloseType::NoFlush);

  EXPECT_CALL(listener1, onDestroy());
  active_listener1.reset();
  EXPECT_CALL(listener2, onDestroy());
  active_listener2.reset();
}
} // namespace
} // namespace Server
} // namespace Envoy
