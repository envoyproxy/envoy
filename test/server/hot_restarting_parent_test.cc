#include <memory>

#include "source/common/network/address_impl.h"
#include "source/server/hot_restarting_child.h"
#include "source/server/hot_restarting_parent.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_manager.h"
#include "test/server/utility.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace {

using HotRestartMessage = envoy::HotRestartMessage;

class MockHotRestartMessageSender : public HotRestartMessageSender {
public:
  MOCK_METHOD(void, sendHotRestartMessage, (envoy::HotRestartMessage && msg));
};

class HotRestartingParentTest : public testing::Test {
public:
  Network::Address::InstanceConstSharedPtr ipv4_test_addr_1_ =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:12345");
  Network::Address::InstanceConstSharedPtr ipv4_test_addr_2_ =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:54321");
  NiceMock<MockInstance> server_;
  MockHotRestartMessageSender message_sender_;
  HotRestartingParent::Internal hot_restarting_parent_{&server_, message_sender_};
};

TEST_F(HotRestartingParentTest, ShutdownAdmin) {
  EXPECT_CALL(server_, shutdownAdmin());
  EXPECT_CALL(server_, startTimeFirstEpoch()).WillOnce(Return(12345));
  HotRestartMessage message = hot_restarting_parent_.shutdownAdmin();
  EXPECT_EQ(12345, message.reply().shutdown_admin().original_start_time_unix_seconds());
}

TEST_F(HotRestartingParentTest, GetListenSocketsForChildNotFound) {
  MockListenerManager listener_manager;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  EXPECT_CALL(server_, listenerManager()).WillOnce(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, listeners(ListenerManager::ListenerState::ACTIVE))
      .WillOnce(Return(listeners));

  HotRestartMessage::Request request;
  request.mutable_pass_listen_socket()->set_address("tcp://127.0.0.1:80");
  HotRestartMessage message = hot_restarting_parent_.getListenSocketsForChild(request);
  EXPECT_EQ(-1, message.reply().pass_listen_socket().fd());
}

TEST_F(HotRestartingParentTest, GetListenSocketsForChildNotBindPort) {
  MockListenerManager listener_manager;
  Network::MockListenerConfig listener_config;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  InSequence s;
  listeners.push_back(std::ref(*static_cast<Network::ListenerConfig*>(&listener_config)));
  EXPECT_CALL(server_, listenerManager()).WillOnce(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, listeners(ListenerManager::ListenerState::ACTIVE))
      .WillOnce(Return(listeners));
  EXPECT_CALL(listener_config, listenSocketFactories());
  Network::Address::InstanceConstSharedPtr address(
      new Network::Address::Ipv4Instance("0.0.0.0", 80));
  EXPECT_CALL(
      *static_cast<Network::MockListenSocketFactory*>(listener_config.socket_factories_[0].get()),
      localAddress())
      .WillOnce(ReturnRef(address));
  EXPECT_CALL(listener_config, bindToPort()).WillOnce(Return(false));

  HotRestartMessage::Request request;
  request.mutable_pass_listen_socket()->set_address("tcp://0.0.0.0:80");
  HotRestartMessage message = hot_restarting_parent_.getListenSocketsForChild(request);
  EXPECT_EQ(-1, message.reply().pass_listen_socket().fd());
}

TEST_F(HotRestartingParentTest, GetListenSocketsForChildSocketType) {
  MockListenerManager listener_manager;
  Network::MockListenerConfig tcp_listener_config;
  Network::MockListenerConfig udp_listener_config;
  MockOptions options;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  InSequence s;

  listeners.push_back(std::ref(*static_cast<Network::ListenerConfig*>(&tcp_listener_config)));
  listeners.push_back(std::ref(*static_cast<Network::ListenerConfig*>(&udp_listener_config)));

  EXPECT_CALL(server_, listenerManager()).WillOnce(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, listeners(ListenerManager::ListenerState::ACTIVE))
      .WillOnce(Return(listeners));
  Network::Address::InstanceConstSharedPtr address(
      new Network::Address::Ipv4Instance("0.0.0.0", 80));
  EXPECT_CALL(tcp_listener_config, listenSocketFactories());
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  tcp_listener_config.socket_factories_[0].get()),
              localAddress())
      .WillOnce(ReturnRef(address));
  EXPECT_CALL(tcp_listener_config, bindToPort()).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  tcp_listener_config.socket_factories_[0].get()),
              socketType())
      .WillOnce(Return(Network::Socket::Type::Stream));

  EXPECT_CALL(udp_listener_config, listenSocketFactories());
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  udp_listener_config.socket_factories_[0].get()),
              localAddress())
      .WillOnce(ReturnRef(address));
  EXPECT_CALL(udp_listener_config, bindToPort()).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  udp_listener_config.socket_factories_[0].get()),
              socketType())
      .WillOnce(Return(Network::Socket::Type::Datagram));

  EXPECT_CALL(server_, options()).WillOnce(ReturnRef(options));
  EXPECT_CALL(options, concurrency()).WillOnce(Return(1));
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  udp_listener_config.socket_factories_[0].get()),
              getListenSocket(_));

  HotRestartMessage::Request request;
  request.mutable_pass_listen_socket()->set_address("udp://0.0.0.0:80");
  HotRestartMessage message = hot_restarting_parent_.getListenSocketsForChild(request);
  EXPECT_EQ(0, message.reply().pass_listen_socket().fd());
}

TEST_F(HotRestartingParentTest, GetListenSocketsWithMultipleAddresses) {
  Network::SocketSharedPtr socket = std::make_shared<NiceMock<Network::MockListenSocket>>();
  MockListenerManager listener_manager;
  Network::MockListenerConfig udp_listener_config;
  MockOptions options;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  InSequence s;

  // Add one more socket factory to mimic two addresses in the listener.
  udp_listener_config.socket_factories_.emplace_back(
      std::make_unique<Network::MockListenSocketFactory>());
  listeners.push_back(std::ref(*static_cast<Network::ListenerConfig*>(&udp_listener_config)));

  EXPECT_CALL(server_, listenerManager()).WillOnce(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, listeners(ListenerManager::ListenerState::ACTIVE))
      .WillOnce(Return(listeners));
  Network::Address::InstanceConstSharedPtr address(
      new Network::Address::Ipv4Instance("0.0.0.0", 80));
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("0.0.0.0", 8080));

  EXPECT_CALL(udp_listener_config, listenSocketFactories());
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  udp_listener_config.socket_factories_[0].get()),
              localAddress())
      .WillOnce(ReturnRef(alt_address));

  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  udp_listener_config.socket_factories_[1].get()),
              localAddress())
      .WillOnce(ReturnRef(address));
  EXPECT_CALL(udp_listener_config, bindToPort()).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  udp_listener_config.socket_factories_[1].get()),
              socketType())
      .WillOnce(Return(Network::Socket::Type::Datagram));

  EXPECT_CALL(server_, options()).WillOnce(ReturnRef(options));
  EXPECT_CALL(options, concurrency()).WillOnce(Return(1));
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  udp_listener_config.socket_factories_[1].get()),
              getListenSocket(_))
      .WillOnce(Return(socket));

  HotRestartMessage::Request request;
  request.mutable_pass_listen_socket()->set_address("udp://0.0.0.0:80");
  HotRestartMessage message = hot_restarting_parent_.getListenSocketsForChild(request);
  EXPECT_EQ(0, message.reply().pass_listen_socket().fd());
}

TEST_F(HotRestartingParentTest, GetListenSocketsForChildUnixDomainSocket) {
  MockListenerManager listener_manager;
  Network::MockListenerConfig listener_config;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  Network::Address::InstanceConstSharedPtr local_address =
      std::make_shared<Network::Address::PipeInstance>("domain.socket");
  MockOptions options;
  InSequence s;

  listeners.push_back(std::ref(*static_cast<Network::ListenerConfig*>(&listener_config)));

  EXPECT_CALL(server_, listenerManager()).WillOnce(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, listeners(ListenerManager::ListenerState::ACTIVE))
      .WillOnce(Return(listeners));
  EXPECT_CALL(listener_config, listenSocketFactories());
  EXPECT_CALL(
      *static_cast<Network::MockListenSocketFactory*>(listener_config.socket_factories_[0].get()),
      localAddress())
      .WillOnce(ReturnRef(local_address));
  EXPECT_CALL(listener_config, bindToPort()).WillOnce(Return(true));
  EXPECT_CALL(
      *static_cast<Network::MockListenSocketFactory*>(listener_config.socket_factories_[0].get()),
      socketType())
      .WillOnce(Return(Network::Socket::Type::Stream));

  EXPECT_CALL(server_, options()).WillOnce(ReturnRef(options));
  EXPECT_CALL(options, concurrency()).WillOnce(Return(1));
  EXPECT_CALL(
      *static_cast<Network::MockListenSocketFactory*>(listener_config.socket_factories_[0].get()),
      getListenSocket(_));

  HotRestartMessage::Request request;
  request.mutable_pass_listen_socket()->set_address("unix://domain.socket");
  HotRestartMessage message = hot_restarting_parent_.getListenSocketsForChild(request);
  EXPECT_EQ(0, message.reply().pass_listen_socket().fd());
}

TEST_F(HotRestartingParentTest, ExportStatsToChild) {
  Stats::TestUtil::TestStore store;
  MockListenerManager listener_manager;
  EXPECT_CALL(server_, listenerManager()).WillRepeatedly(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, numConnections()).WillRepeatedly(Return(0));
  EXPECT_CALL(server_, stats()).WillRepeatedly(ReturnRef(store));

  {
    store.counter("c1").inc();
    store.counter("c2").add(2);
    store.gauge("g0", Stats::Gauge::ImportMode::Accumulate).set(0);
    store.gauge("g1", Stats::Gauge::ImportMode::Accumulate).set(123);
    store.gauge("g2", Stats::Gauge::ImportMode::Accumulate).set(456);
    HotRestartMessage::Reply::Stats stats;
    hot_restarting_parent_.exportStatsToChild(&stats);
    EXPECT_EQ(1, stats.counter_deltas().at("c1"));
    EXPECT_EQ(2, stats.counter_deltas().at("c2"));
    EXPECT_EQ(0, stats.gauges().at("g0"));
    EXPECT_EQ(123, stats.gauges().at("g1"));
    EXPECT_EQ(456, stats.gauges().at("g2"));
  }
  // When a counter has not changed since its last export, it should not be included in the message.
  {
    store.counter("c2").add(2);
    store.gauge("g1", Stats::Gauge::ImportMode::Accumulate).add(1);
    store.gauge("g2", Stats::Gauge::ImportMode::Accumulate).sub(1);
    HotRestartMessage::Reply::Stats stats;
    hot_restarting_parent_.exportStatsToChild(&stats);
    EXPECT_EQ(stats.counter_deltas().end(), stats.counter_deltas().find("c1"));
    EXPECT_EQ(2, stats.counter_deltas().at("c2")); // 4 is the value, but 2 is the delta
    EXPECT_EQ(0, stats.gauges().at("g0"));
    EXPECT_EQ(124, stats.gauges().at("g1"));
    EXPECT_EQ(455, stats.gauges().at("g2"));
  }

  // When a counter and gauge are not used, they should not be included in the message.
  {
    store.counter("unused_counter");
    store.counter("used_counter").inc();
    store.gauge("unused_gauge", Stats::Gauge::ImportMode::Accumulate);
    store.gauge("used_gauge", Stats::Gauge::ImportMode::Accumulate).add(1);
    HotRestartMessage::Reply::Stats stats;
    hot_restarting_parent_.exportStatsToChild(&stats);
    EXPECT_EQ(stats.counter_deltas().end(), stats.counter_deltas().find("unused_counter"));
    EXPECT_EQ(1, stats.counter_deltas().at("used_counter"));
    EXPECT_EQ(stats.gauges().end(), stats.counter_deltas().find("unused_gauge"));
    EXPECT_EQ(1, stats.gauges().at("used_gauge"));
  }
}

TEST_F(HotRestartingParentTest, RetainDynamicStats) {
  MockListenerManager listener_manager;
  Stats::SymbolTableImpl parent_symbol_table;
  Stats::TestUtil::TestStore parent_store(parent_symbol_table);

  EXPECT_CALL(server_, listenerManager()).WillRepeatedly(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, numConnections()).WillRepeatedly(Return(0));
  EXPECT_CALL(server_, stats()).WillRepeatedly(ReturnRef(parent_store));

  HotRestartMessage::Reply::Stats stats_proto;
  {
    Stats::StatNameDynamicPool dynamic(parent_store.symbolTable());
    parent_store.counter("c1").inc();
    parent_store.rootScope()->counterFromStatName(dynamic.add("c2")).inc();
    parent_store.gauge("g1", Stats::Gauge::ImportMode::Accumulate).set(123);
    parent_store.rootScope()
        ->gaugeFromStatName(dynamic.add("g2"), Stats::Gauge::ImportMode::Accumulate)
        .set(42);
    hot_restarting_parent_.exportStatsToChild(&stats_proto);
  }

  {
    Stats::SymbolTableImpl child_symbol_table;
    Stats::TestUtil::TestStore child_store(child_symbol_table);
    Stats::StatNameDynamicPool dynamic(child_store.symbolTable());
    Stats::Counter& c1 = child_store.counter("c1");
    Stats::Counter& c2 = child_store.rootScope()->counterFromStatName(dynamic.add("c2"));
    Stats::Gauge& g1 = child_store.gauge("g1", Stats::Gauge::ImportMode::Accumulate);
    Stats::Gauge& g2 = child_store.rootScope()->gaugeFromStatName(
        dynamic.add("g2"), Stats::Gauge::ImportMode::Accumulate);

    HotRestartingChild hot_restarting_child(0, 0, testDomainSocketName(), 0);
    hot_restarting_child.mergeParentStats(child_store, stats_proto);
    EXPECT_EQ(1, c1.value());
    EXPECT_EQ(1, c2.value());
    EXPECT_EQ(123, g1.value());
    EXPECT_EQ(42, g2.value());
  }
}

MATCHER_P(UdpPacketHandlerPtrIs, expected_handler, "") {
  bool matched = arg->non_dispatched_udp_packet_handler_.ptr() == expected_handler;
  if (!matched) {
    *result_listener << "\n&non_dispatched_udp_packet_handler_ == "
                     << arg->non_dispatched_udp_packet_handler_.ptr()
                     << "\nexpected_handler == " << expected_handler;
  }
  return matched;
}

TEST_F(HotRestartingParentTest, DrainListeners) {
  EXPECT_CALL(server_, drainListeners(UdpPacketHandlerPtrIs(&hot_restarting_parent_)));
  hot_restarting_parent_.drainListeners();
}

TEST_F(HotRestartingParentTest, UdpPacketIsForwarded) {
  uint32_t worker_index = 12; // arbitrary index
  Network::UdpRecvData packet;
  std::string msg = "hello";
  packet.addresses_.local_ = ipv4_test_addr_1_;
  packet.addresses_.peer_ = ipv4_test_addr_2_;
  packet.buffer_ = std::make_unique<Buffer::OwnedImpl>(msg);
  packet.receive_time_ = MonotonicTime(std::chrono::microseconds(1234567890));
  envoy::HotRestartMessage expected_msg;
  auto* expected_packet = expected_msg.mutable_request()->mutable_forwarded_udp_packet();
  expected_packet->set_local_addr("udp://127.0.0.1:12345");
  expected_packet->set_peer_addr("udp://127.0.0.1:54321");
  expected_packet->set_payload(msg);
  expected_packet->set_receive_time_epoch_microseconds(1234567890);
  expected_packet->set_worker_index(worker_index);
  EXPECT_CALL(message_sender_, sendHotRestartMessage(ProtoEq(expected_msg)));
  hot_restarting_parent_.handle(worker_index, packet);
}

} // namespace
} // namespace Server
} // namespace Envoy
