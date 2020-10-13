#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/resource_monitor.h"
#include "envoy/server/resource_monitor_config.h"

#include "common/stats/isolated_store_impl.h"

#include "server/overload_manager_impl.h"

#include "extensions/resource_monitors/common/factory_base.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::Invoke;
using testing::NiceMock;
using testing::Property;

namespace Envoy {
namespace Server {
namespace {

class FakeResourceMonitor : public ResourceMonitor {
public:
  FakeResourceMonitor(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher), response_(0.0) {}

  void setPressure(double pressure) { response_ = pressure; }

  void setError() { response_ = EnvoyException("fake_error"); }

  void setUpdateAsync(bool new_update_async) {
    callbacks_.reset();
    update_async_ = new_update_async;
  }

  void updateResourceUsage(ResourceMonitor::Callbacks& callbacks) override {
    if (update_async_) {
      callbacks_.emplace(callbacks);
    } else {
      publishUpdate(callbacks);
    }
  }

  void publishUpdate() {
    if (update_async_) {
      ASSERT(callbacks_.has_value());
      publishUpdate(*callbacks_);
      callbacks_.reset();
    }
  }

private:
  void publishUpdate(ResourceMonitor::Callbacks& callbacks) {
    if (absl::holds_alternative<double>(response_)) {
      Server::ResourceUsage usage;
      usage.resource_pressure_ = absl::get<double>(response_);
      dispatcher_.post([&, usage]() { callbacks.onSuccess(usage); });
    } else {
      EnvoyException& error = absl::get<EnvoyException>(response_);
      dispatcher_.post([&, error]() { callbacks.onFailure(error); });
    }
  }

  Event::Dispatcher& dispatcher_;
  absl::variant<double, EnvoyException> response_;
  bool update_async_ = false;
  absl::optional<std::reference_wrapper<ResourceMonitor::Callbacks>> callbacks_;
};

template <class ConfigType>
class FakeResourceMonitorFactory : public Server::Configuration::ResourceMonitorFactory {
public:
  FakeResourceMonitorFactory(const std::string& name) : monitor_(nullptr), name_(name) {}

  Server::ResourceMonitorPtr
  createResourceMonitor(const Protobuf::Message&,
                        Server::Configuration::ResourceMonitorFactoryContext& context) override {
    auto monitor = std::make_unique<FakeResourceMonitor>(context.dispatcher());
    monitor_ = monitor.get();
    return monitor;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ConfigType()};
  }

  std::string name() const override { return name_; }

  FakeResourceMonitor* monitor_; // not owned
  const std::string name_;
};

class OverloadManagerImplTest : public testing::Test {
protected:
  OverloadManagerImplTest()
      : factory1_("envoy.resource_monitors.fake_resource1"),
        factory2_("envoy.resource_monitors.fake_resource2"),
        factory3_("envoy.resource_monitors.fake_resource3"),
        factory4_("envoy.resource_monitors.fake_resource4"), register_factory1_(factory1_),
        register_factory2_(factory2_), register_factory3_(factory3_), register_factory4_(factory4_),
        api_(Api::createApiForTest(stats_)) {}

  void setDispatcherExpectation() {
    timer_ = new NiceMock<Event::MockTimer>();
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([&](Event::TimerCb cb) {
      timer_cb_ = cb;
      return timer_;
    }));
  }

  envoy::config::overload::v3::OverloadManager parseConfig(const std::string& config) {
    envoy::config::overload::v3::OverloadManager proto;
    TestUtility::loadFromYaml(config, proto);
    return proto;
  }

  std::unique_ptr<OverloadManagerImpl> createOverloadManager(const std::string& config) {
    return std::make_unique<OverloadManagerImpl>(dispatcher_, stats_, thread_local_,
                                                 parseConfig(config), validation_visitor_, *api_);
  }

  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Struct> factory1_;
  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Timestamp> factory2_;
  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Timestamp> factory3_;
  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Timestamp> factory4_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory1_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory2_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory3_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory4_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Event::MockTimer>* timer_; // not owned
  Stats::TestUtil::TestStore stats_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Event::TimerCb timer_cb_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

constexpr char kRegularStateConfig[] = R"YAML(
  refresh_interval:
    seconds: 1
  resource_monitors:
    - name: envoy.resource_monitors.fake_resource1
    - name: envoy.resource_monitors.fake_resource2
    - name: envoy.resource_monitors.fake_resource3
    - name: envoy.resource_monitors.fake_resource4
  actions:
    - name: envoy.overload_actions.dummy_action
      triggers:
        - name: envoy.resource_monitors.fake_resource1
          threshold:
            value: 0.9
        - name: envoy.resource_monitors.fake_resource2
          threshold:
            value: 0.8
        - name: envoy.resource_monitors.fake_resource3
          scaled:
            scaling_threshold: 0.5
            saturation_threshold: 0.8
        - name: envoy.resource_monitors.fake_resource4
          scaled:
            scaling_threshold: 0.5
            saturation_threshold: 0.8
)YAML";

TEST_F(OverloadManagerImplTest, CallbackOnlyFiresWhenStateChanges) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(kRegularStateConfig));
  bool is_active = false;
  int cb_count = 0;
  manager->registerForAction("envoy.overload_actions.dummy_action", dispatcher_,
                             [&](OverloadActionState state) {
                               is_active = state.isSaturated();
                               cb_count++;
                             });
  manager->registerForAction("envoy.overload_actions.unknown_action", dispatcher_,
                             [&](OverloadActionState) { EXPECT_TRUE(false); });
  manager->start();

  Stats::Gauge& active_gauge = stats_.gauge("overload.envoy.overload_actions.dummy_action.active",
                                            Stats::Gauge::ImportMode::Accumulate);
  Stats::Gauge& scale_percent_gauge =
      stats_.gauge("overload.envoy.overload_actions.dummy_action.scale_percent",
                   Stats::Gauge::ImportMode::Accumulate);
  Stats::Gauge& pressure_gauge1 =
      stats_.gauge("overload.envoy.resource_monitors.fake_resource1.pressure",
                   Stats::Gauge::ImportMode::NeverImport);
  Stats::Gauge& pressure_gauge2 =
      stats_.gauge("overload.envoy.resource_monitors.fake_resource2.pressure",
                   Stats::Gauge::ImportMode::NeverImport);
  const OverloadActionState& action_state =
      manager->getThreadLocalOverloadState().getState("envoy.overload_actions.dummy_action");

  // Update does not exceed fake_resource1 trigger threshold, no callback expected
  factory1_.monitor_->setPressure(0.5);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, 0)));
  EXPECT_EQ(0, cb_count);
  EXPECT_EQ(0, active_gauge.value());
  EXPECT_EQ(0, scale_percent_gauge.value());
  EXPECT_EQ(50, pressure_gauge1.value());

  // Update exceeds fake_resource1 trigger threshold, callback is expected
  factory1_.monitor_->setPressure(0.95);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_TRUE(action_state.isSaturated());
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(1, active_gauge.value());
  EXPECT_EQ(100, scale_percent_gauge.value());
  EXPECT_EQ(95, pressure_gauge1.value());

  // Callback should not be invoked if action state does not change
  factory1_.monitor_->setPressure(0.94);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_TRUE(action_state.isSaturated());
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(94, pressure_gauge1.value());

  // The action is already active for fake_resource1 so no callback expected
  factory2_.monitor_->setPressure(0.9);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_TRUE(action_state.isSaturated());
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(90, pressure_gauge2.value());

  // The action remains active for fake_resource2 so no callback expected
  factory1_.monitor_->setPressure(0.5);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_TRUE(action_state.isSaturated());
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(50, pressure_gauge1.value());
  EXPECT_EQ(90, pressure_gauge2.value());

  // Both become inactive so callback is expected
  factory2_.monitor_->setPressure(0.3);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, 0)));
  EXPECT_EQ(2, cb_count);
  EXPECT_EQ(30, pressure_gauge2.value());

  // Different triggers, both become active, only one callback expected
  factory1_.monitor_->setPressure(0.97);
  factory2_.monitor_->setPressure(0.96);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_TRUE(action_state.isSaturated());
  EXPECT_EQ(3, cb_count);
  EXPECT_EQ(97, pressure_gauge1.value());
  EXPECT_EQ(96, pressure_gauge2.value());

  // Different triggers, both become inactive, only one callback expected
  factory1_.monitor_->setPressure(0.41);
  factory2_.monitor_->setPressure(0.42);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, 0)));
  EXPECT_EQ(4, cb_count);
  EXPECT_EQ(41, pressure_gauge1.value());
  EXPECT_EQ(42, pressure_gauge2.value());

  manager->stop();
}

TEST_F(OverloadManagerImplTest, ScaledTrigger) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();
  const auto& action_state =
      manager->getThreadLocalOverloadState().getState("envoy.overload_actions.dummy_action");
  Stats::Gauge& active_gauge = stats_.gauge("overload.envoy.overload_actions.dummy_action.active",
                                            Stats::Gauge::ImportMode::Accumulate);
  Stats::Gauge& scale_percent_gauge =
      stats_.gauge("overload.envoy.overload_actions.dummy_action.scale_percent",
                   Stats::Gauge::ImportMode::Accumulate);

  factory3_.monitor_->setPressure(0.5);
  timer_cb_();

  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, 0)));
  EXPECT_EQ(0, active_gauge.value());
  EXPECT_EQ(0, scale_percent_gauge.value());

  // The trigger for fake_resource3 is a scaled trigger with a min of 0.5 and a max of 0.8. Set the
  // current pressure value to halfway in that range.
  factory3_.monitor_->setPressure(0.65);
  timer_cb_();

  EXPECT_EQ(action_state.value(), 0.5 /* = 0.65 / (0.8 - 0.5) */);
  EXPECT_EQ(0, active_gauge.value());
  EXPECT_EQ(50, scale_percent_gauge.value());

  factory3_.monitor_->setPressure(0.8);
  timer_cb_();

  EXPECT_TRUE(action_state.isSaturated());
  EXPECT_EQ(1, active_gauge.value());
  EXPECT_EQ(100, scale_percent_gauge.value());

  factory3_.monitor_->setPressure(0.9);
  timer_cb_();

  EXPECT_TRUE(action_state.isSaturated());
  EXPECT_EQ(1, active_gauge.value());
  EXPECT_EQ(100, scale_percent_gauge.value());
}

TEST_F(OverloadManagerImplTest, FailedUpdates) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();
  Stats::Counter& failed_updates =
      stats_.counter("overload.envoy.resource_monitors.fake_resource1.failed_updates");

  factory1_.monitor_->setError();
  timer_cb_();
  EXPECT_EQ(1, failed_updates.value());
  timer_cb_();
  EXPECT_EQ(2, failed_updates.value());

  manager->stop();
}

TEST_F(OverloadManagerImplTest, AggregatesMultipleResourceUpdates) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();

  const OverloadActionState& action_state =
      manager->getThreadLocalOverloadState().getState("envoy.overload_actions.dummy_action");

  factory1_.monitor_->setUpdateAsync(true);

  // Monitor 2 will respond immediately at the timer callback, but that won't push an update to the
  // thread-local state because monitor 1 hasn't finished its update yet.
  factory2_.monitor_->setPressure(1.0);
  timer_cb_();

  EXPECT_FALSE(action_state.isSaturated());

  // Once the last monitor publishes, the change to the action takes effect.
  factory1_.monitor_->publishUpdate();
  EXPECT_TRUE(action_state.isSaturated());
}

TEST_F(OverloadManagerImplTest, DelayedUpdatesAreCoalesced) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();

  const OverloadActionState& action_state =
      manager->getThreadLocalOverloadState().getState("envoy.overload_actions.dummy_action");

  factory3_.monitor_->setUpdateAsync(true);
  factory4_.monitor_->setUpdateAsync(true);

  timer_cb_();
  // When monitor 3 publishes its update, the action won't be visible to the thread-local state
  factory3_.monitor_->setPressure(0.6);
  factory3_.monitor_->publishUpdate();
  EXPECT_EQ(action_state.value(), 0.0);

  // Now when monitor 4 publishes a larger value, the update from monitor 3 is skipped.
  EXPECT_FALSE(action_state.isSaturated());
  factory4_.monitor_->setPressure(0.65);
  factory4_.monitor_->publishUpdate();
  EXPECT_EQ(action_state.value(), 0.5 /* = (0.65 - 0.5) / (0.8 - 0.5) */);
}

TEST_F(OverloadManagerImplTest, FlushesUpdatesEvenWithOneUnresponsive) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();

  const OverloadActionState& action_state =
      manager->getThreadLocalOverloadState().getState("envoy.overload_actions.dummy_action");

  // Set monitor 1 to async, but never publish updates for it.
  factory1_.monitor_->setUpdateAsync(true);

  // Monitor 2 will respond immediately at the timer callback, but that won't push an update to the
  // thread-local state because monitor 1 hasn't finished its update yet.
  factory2_.monitor_->setPressure(1.0);
  timer_cb_();

  EXPECT_FALSE(action_state.isSaturated());
  // A second timer callback will flush the update from monitor 2, even though monitor 1 is
  // unresponsive.
  timer_cb_();
  EXPECT_TRUE(action_state.isSaturated());
}

TEST_F(OverloadManagerImplTest, SkippedUpdates) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();
  Stats::Counter& skipped_updates =
      stats_.counter("overload.envoy.resource_monitors.fake_resource1.skipped_updates");
  Stats::Gauge& pressure_gauge1 =
      stats_.gauge("overload.envoy.resource_monitors.fake_resource1.pressure",
                   Stats::Gauge::ImportMode::NeverImport);

  factory1_.monitor_->setUpdateAsync(true);
  EXPECT_EQ(0, pressure_gauge1.value());
  factory1_.monitor_->setPressure(0.3);

  timer_cb_();
  EXPECT_EQ(0, skipped_updates.value());
  timer_cb_();
  EXPECT_EQ(1, skipped_updates.value());
  timer_cb_();
  EXPECT_EQ(2, skipped_updates.value());

  factory1_.monitor_->publishUpdate();
  EXPECT_EQ(30, pressure_gauge1.value());

  timer_cb_();
  EXPECT_EQ(2, skipped_updates.value());

  manager->stop();
}

TEST_F(OverloadManagerImplTest, DuplicateResourceMonitor) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.fake_resource1"
      - name: "envoy.resource_monitors.fake_resource1"
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate resource monitor .*");
}

TEST_F(OverloadManagerImplTest, DuplicateOverloadAction) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.dummy_action"
      - name: "envoy.overload_actions.dummy_action"
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate overload action .*");
}

// A scaled trigger action's thresholds must conform to scaling < saturation.
TEST_F(OverloadManagerImplTest, ScaledTriggerSaturationLessThanScalingThreshold) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.fake_resource1"
    actions:
      - name: "envoy.overload_actions.dummy_action"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            scaled:
              scaling_threshold: 0.9
              saturation_threshold: 0.8
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "scaling_threshold must be less than saturation_threshold.*");
}

// A scaled trigger action can't have threshold values that are equal.
TEST_F(OverloadManagerImplTest, ScaledTriggerThresholdsEqual) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.fake_resource1"
    actions:
      - name: "envoy.overload_actions.dummy_action"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            scaled:
              scaling_threshold: 0.9
              saturation_threshold: 0.9
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "scaling_threshold must be less than saturation_threshold.*");
}

TEST_F(OverloadManagerImplTest, UnknownTrigger) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.dummy_action"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.9
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Unknown trigger resource .*");
}

TEST_F(OverloadManagerImplTest, DuplicateTrigger) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.fake_resource1"
    actions:
      - name: "envoy.overload_actions.dummy_action"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.9
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.8
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException, "Duplicate trigger .*");
}

TEST_F(OverloadManagerImplTest, Shutdown) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();

  EXPECT_CALL(*timer_, disableTimer());
  manager->stop();
}

} // namespace
} // namespace Server
} // namespace Envoy
