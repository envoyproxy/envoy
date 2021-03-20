#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/udp_listener_config.pb.h"
#include "envoy/network/exception.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/server/active_udp_listener_config.h"
#include "envoy/stats/scope.h"

#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/network/address_impl.h"
#include "common/network/connection_balancer_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/udp_default_writer_config.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/utility.h"

#include "server/active_internal_listener.h"
#include "server/connection_handler_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {
namespace {

class MockInternalListenerCallback : public Network::InternalListenerCallbacks {
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
    auto mock_listener_will_be_moved = std::unique_ptr<Network::MockListener>();
    generic_listener_ = mock_listener_will_be_moved.get();
    internal_listener_ = std::make_shared<ActiveInternalListener>(
        conn_handler_, dispatcher_, std::move(mock_listener_will_be_moved), listener_config_);
  }
  void expectFilterChainFactory() {
    EXPECT_CALL(listener_config_, filterChainFactory())
        .WillRepeatedly(ReturnRef(filter_chain_factory_));
  }
  std::string listener_stat_prefix_{"listener_stat_prefix"};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};

  Network::MockConnectionHandler conn_handler_;
  Network::MockListener* generic_listener_;
  // MockInternalListenerCallback internal_listener_;
  Network::MockListenerConfig listener_config_;
  NiceMock<Network::MockFilterChainManager> manager_;

  NiceMock<Network::MockFilterChainFactory> filter_chain_factory_;
  const std::shared_ptr<Network::MockFilterChain> filter_chain_;

  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;

  std::shared_ptr<ActiveInternalListener> internal_listener_;
};

TEST_F(ActiveInternalListenerTest, BasicInternalListener) { addListener(); }
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
        cb.socket().addressProvider().restoreLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*test_listener_filter, destroy_());
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  // auto* connection = new NiceMock<Network::MockServerConnection>();
  // EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  // EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  // (*listener_callbacks)->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  // EXPECT_EQ(1UL, handler_->numConnections());

  // EXPECT_CALL(*listener, onDestroy());
  // EXPECT_CALL(*access_log_, log(_, _, _, _));
  // auto all_matcher = std::make_shared<Network::MockListenerFilterMatcher>();
  // auto* disabled_listener_filter = new Network::MockListenerFilter();
  // auto* enabled_filter = new Network::MockListenerFilter();
  // EXPECT_CALL(factory_, createListenerFilterChain(_))
  //     .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
  //       manager.addAcceptFilter(all_matcher,
  //       Network::ListenerFilterPtr{disabled_listener_filter});
  //       manager.addAcceptFilter(listener_filter_matcher_,
  //                               Network::ListenerFilterPtr{enabled_filter});
  //       return true;
  //     }));

  // // The all matcher matches any incoming traffic and disables the listener filter.
  // EXPECT_CALL(*all_matcher, matches(_)).WillOnce(Return(true));
  // EXPECT_CALL(*disabled_listener_filter, onAccept(_)).Times(0);

  // // The non matcher acts as if always enabled.
  // EXPECT_CALL(*enabled_filter, onAccept(_)).WillOnce(Return(Network::FilterStatus::Continue));
  // EXPECT_CALL(*disabled_listener_filter, destroy_());
  // EXPECT_CALL(*enabled_filter, destroy_());
  // EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  // EXPECT_CALL(*access_log_, log(_, _, _, _));
  // listener_callbacks->onAccept(std::make_unique<NiceMock<Network::MockConnectionSocket>>());
  // EXPECT_CALL(*listener, onDestroy());
}
} // namespace
} // namespace Server
} // namespace Envoy