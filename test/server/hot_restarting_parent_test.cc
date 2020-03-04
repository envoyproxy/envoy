#include <memory>

#include "server/hot_restarting_child.h"
#include "server/hot_restarting_parent.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace {

using HotRestartMessage = envoy::HotRestartMessage;

class HotRestartingParentTest : public testing::Test {
public:
  NiceMock<MockInstance> server_;
  HotRestartingParent::Internal hot_restarting_parent_{&server_};
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
  EXPECT_CALL(listener_manager, listeners()).WillOnce(Return(listeners));

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
  EXPECT_CALL(listener_manager, listeners()).WillOnce(Return(listeners));
  EXPECT_CALL(listener_config, listenSocketFactory());
  EXPECT_CALL(listener_config.socket_factory_, localAddress());
  EXPECT_CALL(listener_config, bindToPort()).WillOnce(Return(false));

  HotRestartMessage::Request request;
  request.mutable_pass_listen_socket()->set_address("tcp://0.0.0.0:80");
  HotRestartMessage message = hot_restarting_parent_.getListenSocketsForChild(request);
  EXPECT_EQ(-1, message.reply().pass_listen_socket().fd());
}

TEST_F(HotRestartingParentTest, ExportStatsToChild) {
  Stats::IsolatedStoreImpl store;
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
  Stats::IsolatedStoreImpl parent_store(parent_symbol_table);

  EXPECT_CALL(server_, listenerManager()).WillRepeatedly(ReturnRef(listener_manager));
  EXPECT_CALL(listener_manager, numConnections()).WillRepeatedly(Return(0));
  EXPECT_CALL(server_, stats()).WillRepeatedly(ReturnRef(parent_store));

  HotRestartMessage::Reply::Stats stats_proto;
  {
    Stats::StatNameDynamicPool dynamic(parent_store.symbolTable());
    parent_store.counter("c1").inc();
    parent_store.counterFromStatName(dynamic.add("c2")).inc();
    parent_store.gauge("g1", Stats::Gauge::ImportMode::Accumulate).set(123);
    parent_store.gaugeFromStatName(dynamic.add("g2"), Stats::Gauge::ImportMode::Accumulate).set(42);
    hot_restarting_parent_.exportStatsToChild(&stats_proto);
  }

  {
    Stats::SymbolTableImpl child_symbol_table;
    Stats::IsolatedStoreImpl child_store(child_symbol_table);
    Stats::StatNameDynamicPool dynamic(child_store.symbolTable());
    Stats::Counter& c1 = child_store.counter("c1");
    Stats::Counter& c2 = child_store.counterFromStatName(dynamic.add("c2"));
    Stats::Gauge& g1 = child_store.gauge("g1", Stats::Gauge::ImportMode::Accumulate);
    Stats::Gauge& g2 =
        child_store.gaugeFromStatName(dynamic.add("g2"), Stats::Gauge::ImportMode::Accumulate);

    HotRestartingChild hot_restarting_child(0, 0);
    hot_restarting_child.mergeParentStats(child_store, stats_proto);
    EXPECT_EQ(1, c1.value());
    EXPECT_EQ(1, c2.value());
    EXPECT_EQ(123, g1.value());
    EXPECT_EQ(42, g2.value());
  }
}

TEST_F(HotRestartingParentTest, DrainListeners) {
  EXPECT_CALL(server_, drainListeners());
  hot_restarting_parent_.drainListeners();
}

} // namespace
} // namespace Server
} // namespace Envoy
