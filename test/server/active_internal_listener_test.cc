#include <memory>

#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/server/active_internal_listener.h"
#include "source/server/connection_handler_impl.h"

#include "test/mocks/common.h"
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

class MockInternalListenerCallback : public Network::InternalListener {
public:
  MOCK_METHOD(void, onAccept, (Network::ConnectionSocketPtr && socket), ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};
class ActiveInternalListenerTest : public testing::Test,
                                   protected Logger::Loggable<Logger::Id::main> {
public:
  ActiveInternalListenerTest() {
    EXPECT_CALL(listener_config_, listenerScope).Times(testing::AnyNumber());
    EXPECT_CALL(conn_handler_, statPrefix()).WillRepeatedly(ReturnRef(listener_stat_prefix_));
    listener_filter_matcher_ = std::make_shared<NiceMock<Network::MockListenerFilterMatcher>>();
  }
  void addListener() {
    EXPECT_CALL(listener_config_, listenerFiltersTimeout());
    EXPECT_CALL(listener_config_, continueOnListenerFiltersTimeout());
    EXPECT_CALL(listener_config_, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
    EXPECT_CALL(listener_config_, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
    auto mock_listener_will_be_moved = std::make_unique<Network::MockListener>();
    generic_listener_ = mock_listener_will_be_moved.get();
    internal_listener_ = std::make_shared<ActiveInternalListener>(
        conn_handler_, dispatcher_, std::move(mock_listener_will_be_moved), listener_config_);
  }
  Network::Listener* addListenerWithRealNetworkListener() {
    EXPECT_CALL(listener_config_, listenerFiltersTimeout());
    EXPECT_CALL(listener_config_, continueOnListenerFiltersTimeout());
    EXPECT_CALL(listener_config_, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
    EXPECT_CALL(listener_config_, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));

    internal_listener_ =
        std::make_shared<ActiveInternalListener>(conn_handler_, dispatcher_, listener_config_);
    return internal_listener_->listener();
  }
  void expectFilterChainFactory() {
    EXPECT_CALL(listener_config_, filterChainFactory())
        .WillRepeatedly(ReturnRef(filter_chain_factory_));
  }
  std::string listener_stat_prefix_{"listener_stat_prefix"};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  BasicResourceLimitImpl resource_limit_;
  Network::MockConnectionHandler conn_handler_;
  Network::MockListener* generic_listener_;
  Network::MockListenerConfig listener_config_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> filter_chain_factory_;
  std::shared_ptr<Network::MockFilterChain> filter_chain_;
  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;
  std::shared_ptr<ActiveInternalListener> internal_listener_;
};

TEST_F(ActiveInternalListenerTest, BasicInternalListener) {
  addListener();
  EXPECT_CALL(*generic_listener_, onDestroy());
}

TEST_F(ActiveInternalListenerTest, AcceptSocketAndCreateListenerFilter) {
  addListener();
  expectFilterChainFactory();
  Network::MockListenerFilter* test_listener_filter = new Network::MockListenerFilter();
  // FIX-ME: replace by mock socket
  Network::Address::InstanceConstSharedPtr original_dst_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 8080));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(listener_filter_matcher_,
                                Network::ListenerFilterPtr{test_listener_filter});
        return true;
      }));
  EXPECT_CALL(*test_listener_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*test_listener_filter, destroy_());
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_CALL(*generic_listener_, onDestroy());
}

TEST_F(ActiveInternalListenerTest, DestroyListenerClosesActiveSocket) {
  addListener();
  expectFilterChainFactory();
  Network::MockListenerFilter* test_listener_filter = new Network::MockListenerFilter();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle));
  EXPECT_CALL(io_handle, isOpen()).WillOnce(Return(true));

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_,
                                Network::ListenerFilterPtr{test_listener_filter});
        return true;
      }));
  EXPECT_CALL(*test_listener_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));

  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*test_listener_filter, destroy_());
  EXPECT_CALL(*generic_listener_, onDestroy());
  internal_listener_.reset();
}

TEST_F(ActiveInternalListenerTest, AcceptSocketAndCreateNetworkFilter) {
  addListener();
  expectFilterChainFactory();

  Network::MockListenerFilter* test_listener_filter = new Network::MockListenerFilter();
  // FIX-ME: replace by mock socket
  Network::Address::InstanceConstSharedPtr original_dst_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 8080));

  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(listener_filter_matcher_,
                                Network::ListenerFilterPtr{test_listener_filter});
        return true;
      }));
  EXPECT_CALL(*test_listener_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*test_listener_filter, destroy_());
  auto filter_factory_callback = std::make_shared<std::vector<Network::FilterFactoryCb>>();
  filter_chain_ = std::make_shared<NiceMock<Network::MockFilterChain>>();
  auto transport_socket_factory = Network::Test::createRawBufferSocketFactory();

  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  EXPECT_CALL(*filter_chain_, transportSocketFactory)
      .WillOnce(testing::ReturnRef(*transport_socket_factory));
  EXPECT_CALL(*filter_chain_, networkFilterFactories).WillOnce(ReturnRef(*filter_factory_callback));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(conn_handler_, incNumConnections());
  EXPECT_CALL(filter_chain_factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  EXPECT_CALL(listener_config_, perConnectionBufferLimitBytes());
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_CALL(conn_handler_, decNumConnections());
  connection->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_.clearDeferredDeleteList();
  EXPECT_CALL(*generic_listener_, onDestroy());
}

TEST_F(ActiveInternalListenerTest, StopListener) {
  addListener();
  EXPECT_CALL(*generic_listener_, onDestroy());
  internal_listener_->shutdownListener();
}

TEST_F(ActiveInternalListenerTest, PausedListenerAcceptNewSocket) {
  addListenerWithRealNetworkListener();
  internal_listener_->pauseListening();

  expectFilterChainFactory();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager&) -> bool { return true; }));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});
}

TEST_F(ActiveInternalListenerTest, DestroyListenerCloseAllConnections) {
  addListenerWithRealNetworkListener();
  internal_listener_->pauseListening();

  expectFilterChainFactory();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  auto filter_factory_callback = std::make_shared<std::vector<Network::FilterFactoryCb>>();
  filter_chain_ = std::make_shared<NiceMock<Network::MockFilterChain>>();
  auto transport_socket_factory = Network::Test::createRawBufferSocketFactory();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager&) -> bool { return true; }));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  EXPECT_CALL(*filter_chain_, transportSocketFactory)
      .WillOnce(testing::ReturnRef(*transport_socket_factory));
  EXPECT_CALL(*filter_chain_, networkFilterFactories).WillOnce(ReturnRef(*filter_factory_callback));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(conn_handler_, incNumConnections());
  EXPECT_CALL(filter_chain_factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  EXPECT_CALL(listener_config_, perConnectionBufferLimitBytes());
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(conn_handler_, decNumConnections());
  internal_listener_.reset();
}
} // namespace
} // namespace Server
} // namespace Envoy
