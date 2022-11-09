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
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {
namespace {

class MockTcpConnectionHandler : public Network::TcpConnectionHandler,
                                 public Network::MockConnectionHandler {
public:
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::BalancedConnectionHandlerOptRef, getBalancedHandlerByTag,
              (uint64_t listener_tag, const Network::Address::Instance&));
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

  void initialize() {
    EXPECT_CALL(listener_config_, listenerScope).Times(testing::AnyNumber());
    EXPECT_CALL(listener_config_, listenerFiltersTimeout());
    EXPECT_CALL(listener_config_, continueOnListenerFiltersTimeout());
    EXPECT_CALL(listener_config_, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
    EXPECT_CALL(listener_config_, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
    EXPECT_CALL(listener_config_, filterChainFactory())
        .WillRepeatedly(ReturnRef(filter_chain_factory_));
  }

  void initializeWithInspectFilter() {
    initialize();
    filter_ = new NiceMock<Network::MockListenerFilter>(inspect_size_);
    EXPECT_CALL(*filter_, destroy_());
    EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
        .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
          manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{filter_});
          return true;
        }));
    generic_listener_ = std::make_unique<NiceMock<Network::MockListener>>();
    EXPECT_CALL(*generic_listener_, onDestroy());
    Network::Address::InstanceConstSharedPtr address(
        new Network::Address::Ipv4Instance("127.0.0.1", 10001));
    generic_active_listener_ =
        std::make_unique<ActiveTcpListener>(conn_handler_, std::move(generic_listener_), address,
                                            listener_config_, balancer_, runtime_);
    generic_active_listener_->incNumConnections();
    generic_accepted_socket_ = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    EXPECT_CALL(*generic_accepted_socket_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  }

  void initializeWithFilter() {
    initialize();
    filter_ = new NiceMock<Network::MockListenerFilter>();
    EXPECT_CALL(*filter_, destroy_());
    EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
        .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
          manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{filter_});
          return true;
        }));
    generic_listener_ = std::make_unique<NiceMock<Network::MockListener>>();
    EXPECT_CALL(*generic_listener_, onDestroy());
    Network::Address::InstanceConstSharedPtr address(
        new Network::Address::Ipv4Instance("127.0.0.1", 10001));
    generic_active_listener_ =
        std::make_unique<ActiveTcpListener>(conn_handler_, std::move(generic_listener_), address,
                                            listener_config_, balancer_, runtime_);
    generic_active_listener_->incNumConnections();
    generic_accepted_socket_ = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    EXPECT_CALL(*generic_accepted_socket_, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  }

  std::string listener_stat_prefix_{"listener_stat_prefix"};
  std::shared_ptr<Network::MockListenSocketFactory> socket_factory_{
      std::make_shared<Network::MockListenSocketFactory>()};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  BasicResourceLimitImpl resource_limit_;
  NiceMock<MockTcpConnectionHandler> conn_handler_;
  std::unique_ptr<NiceMock<Network::MockListener>> generic_listener_;
  Network::MockListenerConfig listener_config_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> filter_chain_factory_;
  std::shared_ptr<Network::MockFilterChain> filter_chain_;
  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;
  NiceMock<Network::MockConnectionBalancer> balancer_;
  NiceMock<Network::MockListenerFilter>* filter_;
  size_t inspect_size_{128};
  std::unique_ptr<ActiveTcpListener> generic_active_listener_;
  NiceMock<Network::MockIoHandle> io_handle_;
  std::unique_ptr<NiceMock<Network::MockConnectionSocket>> generic_accepted_socket_;
  NiceMock<Runtime::MockLoader> runtime_;
};

/**
 * Execute peek data two times, then filter return successful.
 */
TEST_F(ActiveTcpListenerTest, ListenerFilterWithInspectData) {
  initializeWithInspectFilter();

  // The filter stop the filter iteration and waiting for the data.
  EXPECT_CALL(*filter_, onAccept(_)).WillOnce(Return(Network::FilterStatus::StopIteration));
  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Return(true));

  Event::FileReadyCb file_event_callback;
  // ensure the listener filter buffer will register the file event.
  EXPECT_CALL(io_handle_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));
  generic_active_listener_->onAcceptWorker(std::move(generic_accepted_socket_), false, true);

  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          inspect_size_ / 2, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  // the filter is looking for more data.
  EXPECT_CALL(*filter_, onData(_)).WillOnce(Return(Network::FilterStatus::StopIteration));
  file_event_callback(Event::FileReadyType::Read);
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(
          Api::IoCallUint64Result(inspect_size_, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  // the filter get enough data, then return Network::FilterStatus::Continue
  EXPECT_CALL(*filter_, onData(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(io_handle_, resetFileEvents());
  file_event_callback(Event::FileReadyType::Read);
}

/**
 * The event triggered data peek failed.
 */
TEST_F(ActiveTcpListenerTest, ListenerFilterWithInspectDataFailedWithPeek) {
  initializeWithInspectFilter();

  // The filter stop the filter iteration and waiting for the data.
  EXPECT_CALL(*filter_, onAccept(_)).WillOnce(Return(Network::FilterStatus::StopIteration));

  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Return(true));
  Event::FileReadyCb file_event_callback;
  // ensure the listener filter buffer will register the file event.
  EXPECT_CALL(io_handle_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));
  // calling the onAcceptWorker() to create the ActiveTcpSocket.
  generic_active_listener_->onAcceptWorker(std::move(generic_accepted_socket_), false, true);

  EXPECT_CALL(io_handle_, close)
      .WillOnce(Return(
          ByMove(Api::IoCallUint64Result(0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));

  // peek data failed.
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(
          Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_INTR),
                                                     Network::IoSocketError::deleteIoError)))));

  file_event_callback(Event::FileReadyType::Read);
  EXPECT_EQ(generic_active_listener_->stats_.downstream_listener_filter_error_.value(), 1);
}

/**
 * Multiple filters with different `MaxReadBytes()` value.
 */
TEST_F(ActiveTcpListenerTest, ListenerFilterWithInspectDataMultipleFilters) {
  initialize();

  auto inspect_size1 = 128;
  auto* inspect_data_filter1 = new NiceMock<Network::MockListenerFilter>(inspect_size1);
  EXPECT_CALL(*inspect_data_filter1, destroy_());

  auto inspect_size2 = 512;
  auto* inspect_data_filter2 = new NiceMock<Network::MockListenerFilter>(inspect_size2);
  EXPECT_CALL(*inspect_data_filter2, destroy_());

  auto inspect_size3 = 256;
  auto* inspect_data_filter3 = new NiceMock<Network::MockListenerFilter>(inspect_size3);
  EXPECT_CALL(*inspect_data_filter3, destroy_());

  auto* no_inspect_data_filter = new NiceMock<Network::MockListenerFilter>();
  EXPECT_CALL(*no_inspect_data_filter, destroy_());

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{inspect_data_filter1});
        // Expect the `onData()` callback won't be called for this filter.
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{no_inspect_data_filter});
        // Expect the ListenerFilterBuffer's capacity will be increased for this filter.
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{inspect_data_filter2});
        // Expect the ListenerFilterBuffer's capacity won't be decreased.
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{inspect_data_filter3});
        return true;
      }));

  auto listener = std::make_unique<NiceMock<Network::MockListener>>();
  EXPECT_CALL(*listener, onDestroy());
  Network::NopConnectionBalancerImpl balancer;
  Network::Address::InstanceConstSharedPtr address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  auto active_listener = std::make_unique<ActiveTcpListener>(
      conn_handler_, std::move(listener), address, listener_config_, balancer, runtime_);
  auto accepted_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

  EXPECT_CALL(*accepted_socket, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Return(true));

  EXPECT_CALL(*inspect_data_filter1, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::StopIteration));

  Event::FileReadyCb file_event_callback;
  EXPECT_CALL(io_handle_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));

  active_listener->incNumConnections();
  // Calling the onAcceptWorker() to create the ActiveTcpSocket.
  active_listener->onAcceptWorker(std::move(accepted_socket), false, true);

  EXPECT_CALL(io_handle_, recv)
      .WillOnce([&](void*, size_t size, int) {
        EXPECT_EQ(128, size);
        return Api::IoCallUint64Result(128, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      })
      .WillOnce([&](void*, size_t size, int) {
        EXPECT_EQ(512, size);
        return Api::IoCallUint64Result(512, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      })
      .WillOnce([&](void*, size_t size, int) {
        EXPECT_EQ(512, size);
        return Api::IoCallUint64Result(512, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      });

  EXPECT_CALL(*inspect_data_filter1, onData(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));
  EXPECT_CALL(*no_inspect_data_filter, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*inspect_data_filter2, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::StopIteration));

  file_event_callback(Event::FileReadyType::Read);

  EXPECT_CALL(*inspect_data_filter2, onData(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));
  EXPECT_CALL(*inspect_data_filter3, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::StopIteration));

  file_event_callback(Event::FileReadyType::Read);

  EXPECT_CALL(*inspect_data_filter3, onData(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));

  file_event_callback(Event::FileReadyType::Read);
}

/**
 * Similar with above test, but with different filters order.
 */
TEST_F(ActiveTcpListenerTest, ListenerFilterWithInspectDataMultipleFilters2) {
  initialize();

  auto inspect_size1 = 128;
  auto* inspect_data_filter1 = new NiceMock<Network::MockListenerFilter>(inspect_size1);
  EXPECT_CALL(*inspect_data_filter1, destroy_());

  auto inspect_size2 = 512;
  auto* inspect_data_filter2 = new NiceMock<Network::MockListenerFilter>(inspect_size2);
  EXPECT_CALL(*inspect_data_filter2, destroy_());

  auto inspect_size3 = 256;
  auto* inspect_data_filter3 = new NiceMock<Network::MockListenerFilter>(inspect_size3);
  EXPECT_CALL(*inspect_data_filter3, destroy_());

  auto* no_inspect_data_filter = new NiceMock<Network::MockListenerFilter>();
  EXPECT_CALL(*no_inspect_data_filter, destroy_());

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // There will be no ListenerFilterBuffer created for first filter.
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{no_inspect_data_filter});
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{inspect_data_filter1});
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{inspect_data_filter2});
        manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{inspect_data_filter3});
        return true;
      }));

  auto listener = std::make_unique<NiceMock<Network::MockListener>>();
  EXPECT_CALL(*listener, onDestroy());
  Network::NopConnectionBalancerImpl balancer;
  Network::Address::InstanceConstSharedPtr address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  auto active_listener = std::make_unique<ActiveTcpListener>(
      conn_handler_, std::move(listener), address, listener_config_, balancer, runtime_);
  auto accepted_socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

  EXPECT_CALL(*accepted_socket, ioHandle()).WillRepeatedly(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Return(true));
  Event::FileReadyCb file_event_callback;

  EXPECT_CALL(io_handle_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle_, recv)
      .WillOnce([&](void*, size_t size, int) {
        EXPECT_EQ(128, size);
        return Api::IoCallUint64Result(128, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      })
      .WillOnce([&](void*, size_t size, int) {
        EXPECT_EQ(512, size);
        return Api::IoCallUint64Result(512, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      })
      .WillOnce([&](void*, size_t size, int) {
        EXPECT_EQ(512, size);
        return Api::IoCallUint64Result(512, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      });

  EXPECT_CALL(*no_inspect_data_filter, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::Continue));

  EXPECT_CALL(*inspect_data_filter1, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::StopIteration));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));

  active_listener->incNumConnections();
  // Calling the onAcceptWorker() to create the ActiveTcpSocket.
  active_listener->onAcceptWorker(std::move(accepted_socket), false, true);

  EXPECT_CALL(*inspect_data_filter1, onData(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*inspect_data_filter2, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::StopIteration));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));

  file_event_callback(Event::FileReadyType::Read);

  EXPECT_CALL(*inspect_data_filter2, onData(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*inspect_data_filter3, onAccept(_))
      .WillOnce(Return(Network::FilterStatus::StopIteration));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));

  file_event_callback(Event::FileReadyType::Read);

  EXPECT_CALL(*inspect_data_filter3, onData(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));

  file_event_callback(Event::FileReadyType::Read);
}

/**
 * Trigger the file closed event.
 */
TEST_F(ActiveTcpListenerTest, ListenerFilterWithClose) {
  initializeWithInspectFilter();

  // The filter stop the filter iteration and waiting for the data.
  EXPECT_CALL(*filter_, onAccept(_)).WillOnce(Return(Network::FilterStatus::StopIteration));

  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Return(true));
  Event::FileReadyCb file_event_callback;
  // ensure the listener filter buffer will register the file event.
  EXPECT_CALL(io_handle_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));
  generic_active_listener_->onAcceptWorker(std::move(generic_accepted_socket_), false, true);

  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          inspect_size_ / 2, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  // the filter is looking for more data
  EXPECT_CALL(*filter_, onData(_)).WillOnce(Return(Network::FilterStatus::StopIteration));

  file_event_callback(Event::FileReadyType::Read);

  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(
          ByMove(Api::IoCallUint64Result(0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  EXPECT_CALL(io_handle_, close)
      .WillOnce(Return(
          ByMove(Api::IoCallUint64Result(0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  // emit the read event
  file_event_callback(Event::FileReadyType::Read);
  EXPECT_EQ(generic_active_listener_->stats_.downstream_listener_filter_remote_close_.value(), 1);
}

TEST_F(ActiveTcpListenerTest, ListenerFilterCloseSockets) {
  initializeWithInspectFilter();

  // The filter stop the filter iteration and waiting for the data.
  EXPECT_CALL(*filter_, onAccept(_)).WillOnce(Return(Network::FilterStatus::StopIteration));
  bool is_open = true;

  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Invoke([&is_open]() { return is_open; }));
  Event::FileReadyCb file_event_callback;
  // ensure the listener filter buffer will register the file event.
  EXPECT_CALL(io_handle_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          inspect_size_ / 2, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  // the filter is looking for more data
  EXPECT_CALL(*filter_, onData(_)).WillOnce(Invoke([&is_open]() {
    is_open = false;
    return Network::FilterStatus::StopIteration;
  }));

  generic_active_listener_->onAcceptWorker(std::move(generic_accepted_socket_), false, true);
  // emit the read event
  file_event_callback(Event::FileReadyType::Read);
  EXPECT_EQ(0, generic_active_listener_->sockets().size());
}

TEST_F(ActiveTcpListenerTest, PopulateSNIWhenActiveTcpSocketTimeout) {
  initializeWithInspectFilter();

  // The filter stop the filter iteration and waiting for the data.
  EXPECT_CALL(*filter_, onAccept(_)).WillOnce(Return(Network::FilterStatus::StopIteration));
  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Return(true));

  Event::FileReadyCb file_event_callback;
  // ensure the listener filter buffer will register the file event.
  EXPECT_CALL(io_handle_,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle_, activateFileEvents(Event::FileReadyType::Read));

  absl::string_view server_name = "envoy.io";
  generic_accepted_socket_->connection_info_provider_->setRequestedServerName(server_name);

  generic_active_listener_->onAcceptWorker(std::move(generic_accepted_socket_), false, true);

  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          inspect_size_ / 2, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  // the filter is looking for more data.
  EXPECT_CALL(*filter_, onData(_)).WillOnce(Return(Network::FilterStatus::StopIteration));

  file_event_callback(Event::FileReadyType::Read);

  // get the ActiveTcpSocket pointer before unlink() removed from the link-list.
  ActiveTcpSocket* tcp_socket = generic_active_listener_->sockets().front().get();
  // trigger the onTimeout event manually, since the timer is fake.
  generic_active_listener_->sockets().front()->onTimeout();

  EXPECT_EQ(server_name,
            tcp_socket->streamInfo()->downstreamAddressProvider().requestedServerName());
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
  EXPECT_CALL(listener_config1, listenerScope).Times(testing::AnyNumber());
  EXPECT_CALL(listener_config1, listenerFiltersTimeout());
  EXPECT_CALL(listener_config1, continueOnListenerFiltersTimeout());
  EXPECT_CALL(listener_config1, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
  EXPECT_CALL(listener_config1, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
  EXPECT_CALL(listener_config1, handOffRestoredDestinationConnections())
      .WillRepeatedly(Return(true));

  auto mock_listener_will_be_moved1 = std::make_unique<Network::MockListener>();
  auto& listener1 = *mock_listener_will_be_moved1;
  auto active_listener1 =
      std::make_unique<ActiveTcpListener>(conn_handler_, std::move(mock_listener_will_be_moved1),
                                          normal_address, listener_config1, balancer1, runtime_);

  NiceMock<Network::MockListenerConfig> listener_config2;
  Network::MockConnectionBalancer balancer2;
  EXPECT_CALL(balancer2, registerHandler(_));
  EXPECT_CALL(balancer2, unregisterHandler(_));

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(alt_address));
  EXPECT_CALL(listener_config2, listenerFiltersTimeout());
  EXPECT_CALL(listener_config2, listenerScope).Times(testing::AnyNumber());
  EXPECT_CALL(listener_config2, handOffRestoredDestinationConnections())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(listener_config2, continueOnListenerFiltersTimeout());
  EXPECT_CALL(listener_config2, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
  EXPECT_CALL(listener_config2, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
  auto mock_listener_will_be_moved2 = std::make_unique<Network::MockListener>();
  auto& listener2 = *mock_listener_will_be_moved2;
  auto active_listener2 =
      std::make_shared<ActiveTcpListener>(conn_handler_, std::move(mock_listener_will_be_moved2),
                                          alt_address, listener_config2, balancer2, runtime_);

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
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);

  // 2. Redirect to Listener2.
  EXPECT_CALL(conn_handler_, getBalancedHandlerByAddress(_))
      .WillOnce(Return(Network::BalancedConnectionHandlerOptRef(*active_listener2)));

  // 3. Listener2 re-balance. Set the balance target to the the active listener itself.
  EXPECT_CALL(balancer2, pickTargetHandler(_))
      .WillOnce(testing::DoAll(
          testing::WithArg<0>(Invoke([](auto& target) { target.incNumConnections(); })),
          ReturnRef(*active_listener2)));

  auto filter_factory_callback = std::make_shared<std::vector<Network::FilterFactoryCb>>();
  auto transport_socket_factory = Network::Test::createRawBufferDownstreamSocketFactory();
  filter_chain_ = std::make_shared<NiceMock<Network::MockFilterChain>>();

  EXPECT_CALL(conn_handler_, incNumConnections());
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
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

TEST_F(ActiveTcpListenerTest, Rebalance) {
  NiceMock<Network::MockListenerConfig> listener_config1;
  NiceMock<Network::MockConnectionBalancer> balancer1;
  EXPECT_CALL(balancer1, registerHandler(_)).Times(2);
  EXPECT_CALL(balancer1, unregisterHandler(_)).Times(2);

  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  EXPECT_CALL(listener_config1, listenerScope).Times(testing::AnyNumber());
  EXPECT_CALL(listener_config1, listenerFiltersTimeout());
  EXPECT_CALL(listener_config1, continueOnListenerFiltersTimeout());
  EXPECT_CALL(listener_config1, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
  EXPECT_CALL(listener_config1, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
  EXPECT_CALL(listener_config1, handOffRestoredDestinationConnections())
      .WillRepeatedly(Return(true));

  auto mock_listener_will_be_moved1 = std::make_unique<Network::MockListener>();
  auto& listener1 = *mock_listener_will_be_moved1;
  auto active_listener1 =
      std::make_unique<ActiveTcpListener>(conn_handler_, std::move(mock_listener_will_be_moved1),
                                          normal_address, listener_config1, balancer1, runtime_);

  NiceMock<Network::MockListenerConfig> listener_config2;

  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  EXPECT_CALL(listener_config2, listenerFiltersTimeout());
  EXPECT_CALL(listener_config2, listenerScope).Times(testing::AnyNumber());
  EXPECT_CALL(listener_config2, handOffRestoredDestinationConnections())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(listener_config2, continueOnListenerFiltersTimeout());
  EXPECT_CALL(listener_config2, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
  EXPECT_CALL(listener_config2, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
  auto mock_listener_will_be_moved2 = std::make_unique<Network::MockListener>();
  auto& listener2 = *mock_listener_will_be_moved2;
  auto active_listener2 =
      std::make_shared<ActiveTcpListener>(conn_handler_, std::move(mock_listener_will_be_moved2),
                                          normal_address, listener_config2, balancer1, runtime_);
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  // active_listener1 re-balance. Set the balance target to the the active_listener2.
  EXPECT_CALL(balancer1, pickTargetHandler(_))
      .WillOnce(testing::DoAll(testing::WithArg<0>(Invoke([&active_listener2](auto&) {
                                 active_listener2->incNumConnections();
                               })),
                               ReturnRef(*active_listener2)));

  EXPECT_CALL(conn_handler_, getBalancedHandlerByTag)
      .WillOnce(Invoke([&normal_address,
                        &active_listener2](uint64_t, const Network::Address::Instance& address) {
        EXPECT_EQ(address, *normal_address);
        return Network::BalancedConnectionHandlerOptRef(*active_listener2);
      }));
  auto filter_factory_callback = std::make_shared<std::vector<Network::FilterFactoryCb>>();
  auto transport_socket_factory = Network::Test::createRawBufferDownstreamSocketFactory();
  filter_chain_ = std::make_shared<NiceMock<Network::MockFilterChain>>();

  EXPECT_CALL(conn_handler_, incNumConnections());
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
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
