#include <chrono>
#include <initializer_list>

#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/server/overload/thread_local_overload_state.h"
#include "envoy/server/resource_monitor.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/resource_monitors/common/factory_base.h"
#include "source/server/null_overload_manager.h"
#include "source/server/overload_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/options.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::AnyNumber;
using testing::ByMove;
using testing::DoAll;
using testing::FloatNear;
using testing::Invoke;
using testing::NiceMock;
using testing::Pointee;
using testing::Property;
using testing::Return;
using testing::SaveArg;
using testing::UnorderedElementsAreArray;

namespace Envoy {
namespace Server {
namespace {

using TimerType = Event::ScaledTimerType;

class FakeResourceMonitor : public ResourceMonitor {
public:
  FakeResourceMonitor(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher), response_(0.0) {}

  void setPressure(double pressure) { response_ = pressure; }

  void setError() { response_ = EnvoyException("fake_error"); }

  void setUpdateAsync(bool new_update_async) {
    callbacks_.reset();
    update_async_ = new_update_async;
  }

  void updateResourceUsage(ResourceUpdateCallbacks& callbacks) override {
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
  void publishUpdate(ResourceUpdateCallbacks& callbacks) {
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
  absl::optional<std::reference_wrapper<ResourceUpdateCallbacks>> callbacks_;
};

class FakeProactiveResourceMonitor : public ProactiveResourceMonitor {
public:
  FakeProactiveResourceMonitor(uint64_t max) : max_(max), current_(0){};

  bool tryAllocateResource(int64_t increment) override {
    int64_t new_val = (current_ += increment);
    if (new_val > static_cast<int64_t>(max_) || new_val < 0) {
      current_ -= increment;
      return false;
    }
    return true;
  }

  bool tryDeallocateResource(int64_t decrement) override {
    RELEASE_ASSERT(decrement <= current_,
                   "Cannot deallocate resource, current resource usage is lower than decrement");
    int64_t new_val = (current_ -= decrement);
    if (new_val < 0) {
      current_ += decrement;
      return false;
    }
    return true;
  }

  int64_t currentResourceUsage() const override { return current_.load(); }
  int64_t maxResourceUsage() const override { return max_; }

private:
  int64_t max_;
  std::atomic<int64_t> current_;
};

template <class ConfigType>
class FakeResourceMonitorFactory : public Server::Configuration::ResourceMonitorFactory {
public:
  FakeResourceMonitorFactory(const std::string& name) : name_(name) {}

  Server::ResourceMonitorPtr
  createResourceMonitor(const Protobuf::Message&,
                        Server::Configuration::ResourceMonitorFactoryContext& context) override {
    auto monitor = std::make_unique<FakeResourceMonitor>(context.mainThreadDispatcher());
    monitor_ = monitor.get();
    return monitor;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ConfigType()};
  }

  std::string name() const override { return name_; }

  FakeResourceMonitor* monitor_{nullptr}; // not owned
  const std::string name_;
};

template <class ConfigType>
class FakeProactiveResourceMonitorFactory
    : public Server::Configuration::ProactiveResourceMonitorFactory {
public:
  FakeProactiveResourceMonitorFactory(const std::string& name) : name_(name) {}

  Server::ProactiveResourceMonitorPtr
  createProactiveResourceMonitor(const Protobuf::Message&,
                                 Server::Configuration::ResourceMonitorFactoryContext&) override {
    auto monitor = std::make_unique<FakeProactiveResourceMonitor>(3);
    monitor_ = monitor.get();
    return monitor;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ConfigType()};
  }

  std::string name() const override { return name_; }

  FakeProactiveResourceMonitor* monitor_{nullptr}; // not owned
  const std::string name_;
};

class TestOverloadManager : public OverloadManagerImpl {
public:
  TestOverloadManager(Event::Dispatcher& dispatcher, Stats::Scope& stats_scope,
                      ThreadLocal::SlotAllocator& slot_allocator,
                      const envoy::config::overload::v3::OverloadManager& config,
                      ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                      const Server::MockOptions& options, absl::Status& creation_status)
      : OverloadManagerImpl(dispatcher, stats_scope, slot_allocator, config, validation_visitor,
                            api, options, creation_status) {
    THROW_IF_NOT_OK(creation_status);
    EXPECT_CALL(*this, createScaledRangeTimerManager)
        .Times(AnyNumber())
        .WillRepeatedly(Invoke(this, &TestOverloadManager::createDefaultScaledRangeTimerManager));
  }

  MOCK_METHOD(Event::ScaledRangeTimerManagerPtr, createScaledRangeTimerManager,
              (Event::Dispatcher&, const Event::ScaledTimerTypeMapConstSharedPtr&),
              (const, override));

private:
  Event::ScaledRangeTimerManagerPtr createDefaultScaledRangeTimerManager(
      Event::Dispatcher& dispatcher,
      const Event::ScaledTimerTypeMapConstSharedPtr& timer_minimums) const {
    return OverloadManagerImpl::createScaledRangeTimerManager(dispatcher, timer_minimums);
  }
};

class OverloadManagerImplTest : public testing::Test {
protected:
  OverloadManagerImplTest()
      : factory1_("envoy.resource_monitors.fake_resource1"),
        factory2_("envoy.resource_monitors.fake_resource2"),
        factory3_("envoy.resource_monitors.fake_resource3"),
        factory4_("envoy.resource_monitors.fake_resource4"),
        factory5_("envoy.resource_monitors.global_downstream_max_connections"),
        register_factory1_(factory1_), register_factory2_(factory2_), register_factory3_(factory3_),
        register_factory4_(factory4_), register_factory5_(factory5_),
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

  std::unique_ptr<TestOverloadManager> createOverloadManager(const std::string& config) {
    absl::Status creation_status = absl::OkStatus();
    return std::make_unique<TestOverloadManager>(dispatcher_, *stats_.rootScope(), thread_local_,
                                                 parseConfig(config), validation_visitor_, *api_,
                                                 options_, creation_status);
  }

  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Struct> factory1_;
  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Timestamp> factory2_;
  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Duration> factory3_;
  FakeResourceMonitorFactory<Envoy::ProtobufWkt::StringValue> factory4_;
  FakeProactiveResourceMonitorFactory<Envoy::ProtobufWkt::BoolValue> factory5_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory1_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory2_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory3_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory4_;
  Registry::InjectFactory<Configuration::ProactiveResourceMonitorFactory> register_factory5_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Event::MockTimer>* timer_; // not owned
  Stats::TestUtil::TestStore stats_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Event::TimerCb timer_cb_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  Server::MockOptions options_;
};

constexpr char kRegularStateConfig[] = R"YAML(
  refresh_interval:
    seconds: 1
  resource_monitors:
    - name: envoy.resource_monitors.fake_resource1
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
    - name: envoy.resource_monitors.fake_resource2
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Timestamp
    - name: envoy.resource_monitors.fake_resource3
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Duration
    - name: envoy.resource_monitors.fake_resource4
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  actions:
    - name: envoy.overload_actions.stop_accepting_requests
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

constexpr char proactiveResourceConfig[] = R"YAML(
  refresh_interval:
    seconds: 1
  resource_monitors:
    - name: envoy.resource_monitors.fake_resource1
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
    - name: envoy.resource_monitors.global_downstream_max_connections
      typed_config:
        "@type": type.googleapis.com/google.protobuf.BoolValue
  actions:
    - name: envoy.overload_actions.shrink_heap
      triggers:
        - name: envoy.resource_monitors.fake_resource1
          threshold:
            value: 0.9
)YAML";

TEST_F(OverloadManagerImplTest, CallbackOnlyFiresWhenStateChanges) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(kRegularStateConfig));
  bool is_active = false;
  int cb_count = 0;
  manager->registerForAction("envoy.overload_actions.stop_accepting_requests", dispatcher_,
                             [&](OverloadActionState state) {
                               is_active = state.isSaturated();
                               cb_count++;
                             });
  // This overload action callback should never be fired as the action is
  // unknown and unconfigured to a trigger.
  manager->registerForAction("envoy.overload_actions.unknown_action", dispatcher_,
                             [&](OverloadActionState) { EXPECT_TRUE(false); });
  manager->start();

  EXPECT_FALSE(manager->getThreadLocalOverloadState().isResourceMonitorEnabled(
      OverloadProactiveResourceName::GlobalDownstreamMaxConnections));

  Stats::Gauge& active_gauge =
      stats_.gauge("overload.envoy.overload_actions.stop_accepting_requests.active",
                   Stats::Gauge::ImportMode::Accumulate);
  Stats::Gauge& scale_percent_gauge =
      stats_.gauge("overload.envoy.overload_actions.stop_accepting_requests.scale_percent",
                   Stats::Gauge::ImportMode::Accumulate);
  Stats::Gauge& pressure_gauge1 =
      stats_.gauge("overload.envoy.resource_monitors.fake_resource1.pressure",
                   Stats::Gauge::ImportMode::NeverImport);
  Stats::Gauge& pressure_gauge2 =
      stats_.gauge("overload.envoy.resource_monitors.fake_resource2.pressure",
                   Stats::Gauge::ImportMode::NeverImport);
  const OverloadActionState& action_state = manager->getThreadLocalOverloadState().getState(
      "envoy.overload_actions.stop_accepting_requests");

  // Update does not exceed fake_resource1 trigger threshold, no callback expected
  factory1_.monitor_->setPressure(0.5);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, UnitFloat::min())));
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
                                  Property(&OverloadActionState::value, UnitFloat::min())));
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
                                  Property(&OverloadActionState::value, UnitFloat::min())));
  EXPECT_EQ(4, cb_count);
  EXPECT_EQ(41, pressure_gauge1.value());
  EXPECT_EQ(42, pressure_gauge2.value());

  manager->stop();
}

TEST_F(OverloadManagerImplTest, ScaledTrigger) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();
  const auto& action_state = manager->getThreadLocalOverloadState().getState(
      "envoy.overload_actions.stop_accepting_requests");
  Stats::Gauge& active_gauge =
      stats_.gauge("overload.envoy.overload_actions.stop_accepting_requests.active",
                   Stats::Gauge::ImportMode::Accumulate);
  Stats::Gauge& scale_percent_gauge =
      stats_.gauge("overload.envoy.overload_actions.stop_accepting_requests.scale_percent",
                   Stats::Gauge::ImportMode::Accumulate);

  factory3_.monitor_->setPressure(0.5);
  timer_cb_();

  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, UnitFloat::min())));
  EXPECT_EQ(0, active_gauge.value());
  EXPECT_EQ(0, scale_percent_gauge.value());

  // The trigger for fake_resource3 is a scaled trigger with a min of 0.5 and a max of 0.8. Set
  // the current pressure value to halfway in that range.
  factory3_.monitor_->setPressure(0.65);
  timer_cb_();

  EXPECT_EQ(action_state.value(), UnitFloat(0.5) /* = 0.65 / (0.8 - 0.5) */);
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

  const OverloadActionState& action_state = manager->getThreadLocalOverloadState().getState(
      "envoy.overload_actions.stop_accepting_requests");

  factory1_.monitor_->setUpdateAsync(true);

  // Monitor 2 will respond immediately at the timer callback, but that won't push an update to
  // the thread-local state because monitor 1 hasn't finished its update yet.
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

  const OverloadActionState& action_state = manager->getThreadLocalOverloadState().getState(
      "envoy.overload_actions.stop_accepting_requests");

  factory3_.monitor_->setUpdateAsync(true);
  factory4_.monitor_->setUpdateAsync(true);

  timer_cb_();
  // When monitor 3 publishes its update, the action won't be visible to the thread-local state
  factory3_.monitor_->setPressure(0.6);
  factory3_.monitor_->publishUpdate();
  EXPECT_EQ(action_state.value(), UnitFloat::min());

  // Now when monitor 4 publishes a larger value, the update from monitor 3 is skipped.
  EXPECT_FALSE(action_state.isSaturated());
  factory4_.monitor_->setPressure(0.65);
  factory4_.monitor_->publishUpdate();
  EXPECT_EQ(action_state.value(), UnitFloat(0.5) /* = (0.65 - 0.5) / (0.8 - 0.5) */);
}

TEST_F(OverloadManagerImplTest, FlushesUpdatesEvenWithOneUnresponsive) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();

  const OverloadActionState& action_state = manager->getThreadLocalOverloadState().getState(
      "envoy.overload_actions.stop_accepting_requests");

  // Set monitor 1 to async, but never publish updates for it.
  factory1_.monitor_->setUpdateAsync(true);

  // Monitor 2 will respond immediately at the timer callback, but that won't push an update to
  // the thread-local state because monitor 1 hasn't finished its update yet.
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

constexpr char kReducedTimeoutsConfig[] = R"YAML(
  refresh_interval:
    seconds: 1
  resource_monitors:
    - name: envoy.resource_monitors.fake_resource1
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
  actions:
    - name: envoy.overload_actions.reduce_timeouts
      typed_config:
        "@type": type.googleapis.com/envoy.config.overload.v3.ScaleTimersOverloadActionConfig
        timer_scale_factors:
          - timer: HTTP_DOWNSTREAM_CONNECTION_IDLE
            min_timeout: 2s
          - timer: HTTP_DOWNSTREAM_STREAM_IDLE
            min_scale: { value: 10 } # percent
          - timer: TRANSPORT_SOCKET_CONNECT
            min_scale: { value: 40 } # percent
      triggers:
        - name: "envoy.resource_monitors.fake_resource1"
          scaled:
            scaling_threshold: 0.5
            saturation_threshold: 1.0
  )YAML";

// These are the timer types according to the reduced timeouts config above.
constexpr std::pair<TimerType, Event::ScaledTimerMinimum> kReducedTimeoutsMinimums[]{
    {TimerType::HttpDownstreamIdleConnectionTimeout,
     Event::AbsoluteMinimum(std::chrono::seconds(2))},
    {TimerType::HttpDownstreamIdleStreamTimeout, Event::ScaledMinimum(UnitFloat(0.1))},
    {TimerType::TransportSocketConnectTimeout, Event::ScaledMinimum(UnitFloat(0.4))},
};
TEST_F(OverloadManagerImplTest, CreateScaledTimerManager) {
  auto manager(createOverloadManager(kReducedTimeoutsConfig));

  auto* mock_scaled_timer_manager = new Event::MockScaledRangeTimerManager();

  Event::ScaledTimerTypeMapConstSharedPtr timer_minimums;
  EXPECT_CALL(*manager, createScaledRangeTimerManager)
      .WillOnce(
          DoAll(SaveArg<1>(&timer_minimums),
                Return(ByMove(Event::ScaledRangeTimerManagerPtr{mock_scaled_timer_manager}))));

  Event::MockDispatcher mock_dispatcher;
  auto scaled_timer_manager = manager->scaledTimerFactory()(mock_dispatcher);

  EXPECT_EQ(scaled_timer_manager.get(), mock_scaled_timer_manager);
  EXPECT_THAT(timer_minimums, Pointee(UnorderedElementsAreArray(kReducedTimeoutsMinimums)));
}

TEST_F(OverloadManagerImplTest, AdjustScaleFactor) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(kReducedTimeoutsConfig));

  auto* mock_scaled_timer_manager = new Event::MockScaledRangeTimerManager();
  EXPECT_CALL(*manager, createScaledRangeTimerManager)
      .WillOnce(Return(ByMove(Event::ScaledRangeTimerManagerPtr{mock_scaled_timer_manager})));

  Event::MockDispatcher mock_dispatcher;
  auto scaled_timer_manager = manager->scaledTimerFactory()(mock_dispatcher);

  manager->start();

  EXPECT_CALL(mock_dispatcher, post).WillOnce([](Event::PostCb cb) { cb(); });
  // The scaled trigger has range [0.5, 1.0] so 0.6 should map to a scale value of 0.2, which means
  // a timer scale factor of 0.8 (1 - 0.2).
  EXPECT_CALL(*mock_scaled_timer_manager,
              setScaleFactor(Property(&UnitFloat::value, FloatNear(0.8, 0.00001))));
  factory1_.monitor_->setPressure(0.6);

  timer_cb_();
}

TEST_F(OverloadManagerImplTest, DuplicateResourceMonitor) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.fake_resource1"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: "envoy.resource_monitors.fake_resource1"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate resource monitor .*");
}

TEST_F(OverloadManagerImplTest, DuplicateProactiveResourceMonitor) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.global_downstream_max_connections"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.BoolValue
      - name: "envoy.resource_monitors.global_downstream_max_connections"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.BoolValue
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate resource monitor .*");
}

TEST_F(OverloadManagerImplTest, DuplicateOverloadAction) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.shrink_heap"
      - name: "envoy.overload_actions.shrink_heap"
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate overload action .*");
}

TEST_F(OverloadManagerImplTest, ActionWithUnexpectedTypedConfig) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.shrink_heap"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Empty
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          ".* unexpected .* typed_config .*");
}

TEST_F(OverloadManagerImplTest, ReduceTimeoutsWithoutAction) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.reduce_timeouts"
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Unable to unpack as .*ScaleTimersOverloadActionConfig");
}

TEST_F(OverloadManagerImplTest, ReduceTimeoutsWithWrongTypedConfigMessage) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.reduce_timeouts"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Empty
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Unable to unpack as .*ScaleTimersOverloadActionConfig");
}

TEST_F(OverloadManagerImplTest, ReduceTimeoutsWithNoTimersSpecified) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.reduce_timeouts"
        typed_config:
          "@type": type.googleapis.com/envoy.config.overload.v3.ScaleTimersOverloadActionConfig
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          ".* constraint validation failed.*");
}

// A scaled trigger action's thresholds must conform to scaling < saturation.
TEST_F(OverloadManagerImplTest, ScaledTriggerSaturationLessThanScalingThreshold) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.fake_resource1"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    actions:
      - name: "envoy.overload_actions.shrink_heap"
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
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    actions:
      - name: "envoy.overload_actions.shrink_heap"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            scaled:
              scaling_threshold: 0.9
              saturation_threshold: 0.9
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "scaling_threshold must be less than saturation_threshold.*");
}

TEST_F(OverloadManagerImplTest, UnknownActionShouldError) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: "envoy.resource_monitors.fake_resource1"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    actions:
      - name: "envoy.overload_actions.not_a_valid_action"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.9
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Unknown Overload Manager Action .*");
}

TEST_F(OverloadManagerImplTest, UnknownTrigger) {
  const std::string config = R"EOF(
    actions:
      - name: "envoy.overload_actions.shrink_heap"
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
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    actions:
      - name: "envoy.overload_actions.shrink_heap"
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

TEST_F(OverloadManagerImplTest, ShouldThrowIfUsingResetStreamsWithoutBufferFactoryConfig) {
  const std::string lower_greater_than_upper_config = R"EOF(
  actions:
    - name: envoy.overload_actions.reset_high_memory_stream
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(lower_greater_than_upper_config), EnvoyException,
                          "Overload action .* requires buffer_factory_config.");
}

TEST_F(OverloadManagerImplTest, Shutdown) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();

  EXPECT_CALL(*timer_, disableTimer());
  manager->stop();
}

TEST_F(OverloadManagerImplTest, MissingConfigTriggerType) {
  constexpr char missingTriggerTypeConfig[] = R"YAML(
  actions:
    - name: envoy.overload_actions.shrink_heap
      triggers:
        - name: envoy.resource_monitors.fake_resource1
)YAML";
  EXPECT_THROW_WITH_REGEX(createOverloadManager(missingTriggerTypeConfig), EnvoyException,
                          "action not set for trigger.*");
}

TEST_F(OverloadManagerImplTest, ProactiveResourceAllocateAndDeallocateResourceTest) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(proactiveResourceConfig));
  Stats::Counter& failed_updates =
      stats_.counter("overload.envoy.resource_monitors.global_downstream_max_connections."
                     "failed_updates");
  manager->start();
  EXPECT_TRUE(manager->getThreadLocalOverloadState().isResourceMonitorEnabled(
      OverloadProactiveResourceName::GlobalDownstreamMaxConnections));
  bool resource_allocated = manager->getThreadLocalOverloadState().tryAllocateResource(
      Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections, 1);
  EXPECT_TRUE(resource_allocated);
  auto monitor = manager->getThreadLocalOverloadState().getProactiveResourceMonitorForTest(
      Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections);
  EXPECT_NE(absl::nullopt, monitor);
  EXPECT_EQ(1, monitor->currentResourceUsage());
  resource_allocated = manager->getThreadLocalOverloadState().tryAllocateResource(
      Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections, 3);
  EXPECT_FALSE(resource_allocated);
  EXPECT_EQ(1, failed_updates.value());

  bool resource_deallocated = manager->getThreadLocalOverloadState().tryDeallocateResource(
      Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections, 1);
  EXPECT_TRUE(resource_deallocated);
  EXPECT_DEATH(manager->getThreadLocalOverloadState().tryDeallocateResource(
                   Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections, 1),
               ".*Cannot deallocate resource, current resource usage is lower than decrement.*");
  manager->stop();
}

class OverloadManagerSimulatedTimeTest : public OverloadManagerImplTest,
                                         public Envoy::Event::TestUsingSimulatedTime {};

TEST_F(OverloadManagerSimulatedTimeTest, RefreshLoopDelay) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(kRegularStateConfig));
  manager->start();

  simTime().advanceTimeWait(Envoy::Seconds(1));

  timer_cb_();

  // Check the first reading
  const std::vector<uint64_t> first_reading =
      stats_.histogramValues("overload.refresh_interval_delay", false);
  EXPECT_EQ(first_reading.size(), 1);
  EXPECT_EQ(first_reading[0], 1000);

  simTime().advanceTimeWait(Envoy::Seconds(2));

  timer_cb_();

  // Check the second reading
  const std::vector<uint64_t> second_reading =
      stats_.histogramValues("overload.refresh_interval_delay", false);
  EXPECT_EQ(second_reading.size(), 2);
  EXPECT_EQ(second_reading[1], 2000);

  manager->stop();
}

class OverloadManagerLoadShedPointImplTest : public OverloadManagerImplTest {};

TEST_F(OverloadManagerLoadShedPointImplTest, DuplicateLoadShedPoints) {
  const std::string config = R"EOF(
    loadshed_points:
      - name: "envoy.load_shed_point.dummy_point"
      - name: "envoy.load_shed_point.dummy_point"
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate loadshed point .*");
}

TEST_F(OverloadManagerLoadShedPointImplTest, UnknownResource) {
  const std::string config = R"EOF(
    loadshed_points:
      - name: "envoy.load_shed_point.dummy_point"
        triggers:
          - name: "envoy.resource_monitors.unknown_resource"
            threshold:
              value: 0.9
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Unknown trigger resource .* for loadshed point .*");
}

TEST_F(OverloadManagerLoadShedPointImplTest, ThrowsIfDuplicateTrigger) {
  const std::string config = R"EOF(
    resource_monitors:
      - name: envoy.resource_monitors.fake_resource1
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    loadshed_points:
      - name: "envoy.load_shed_point.dummy_point"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.9
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.7
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate trigger resource for LoadShedPoint .*");
}

TEST_F(OverloadManagerLoadShedPointImplTest, ReturnsNullIfNonExistentLoadShedPointRequested) {
  setDispatcherExpectation();
  const std::string config = R"EOF(
    resource_monitors:
      - name: envoy.resource_monitors.fake_resource1
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    loadshed_points:
      - name: "test_point"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.9
  )EOF";

  auto manager{createOverloadManager(config)};
  manager->start();
  LoadShedPoint* point = manager->getLoadShedPoint("non_existent_point");
  EXPECT_EQ(point, nullptr);
}

TEST_F(OverloadManagerLoadShedPointImplTest, PointUsesTriggerToDetermineWhetherToLoadShed) {
  setDispatcherExpectation();
  const std::string config = R"EOF(
    resource_monitors:
      - name: envoy.resource_monitors.fake_resource1
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    loadshed_points:
      - name: "test_point"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.9
  )EOF";

  auto manager{createOverloadManager(config)};
  manager->start();

  LoadShedPoint* point = manager->getLoadShedPoint("test_point");
  ASSERT_NE(point, nullptr);

  Stats::Gauge& scale_percent =
      stats_.gauge("overload.test_point.scale_percent", Stats::Gauge::ImportMode::Accumulate);

  EXPECT_EQ(0, scale_percent.value());
  EXPECT_FALSE(point->shouldShedLoad());

  factory1_.monitor_->setPressure(0.65);
  timer_cb_();
  EXPECT_EQ(0, scale_percent.value());
  EXPECT_FALSE(point->shouldShedLoad());

  factory1_.monitor_->setPressure(0.95);
  timer_cb_();
  EXPECT_TRUE(point->shouldShedLoad());
  EXPECT_EQ(100, scale_percent.value());

  factory1_.monitor_->setPressure(0.7);
  timer_cb_();
  EXPECT_FALSE(point->shouldShedLoad());
  EXPECT_EQ(0, scale_percent.value());
}

TEST_F(OverloadManagerLoadShedPointImplTest, TriggerLoadShedCunterTest) {
  setDispatcherExpectation();
  const std::string config = R"EOF(
    resource_monitors:
      - name: envoy.resource_monitors.fake_resource1
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    loadshed_points:
      - name: "test_point"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            scaled:
              scaling_threshold: 0.8
              saturation_threshold: 0.9
  )EOF";

  auto manager{createOverloadManager(config)};
  manager->start();

  LoadShedPoint* point = manager->getLoadShedPoint("test_point");
  ASSERT_NE(point, nullptr);

  Stats::Counter& shed_load_count = stats_.counter("overload.test_point.shed_load_count");

  EXPECT_EQ(0, shed_load_count.value());
  EXPECT_FALSE(point->shouldShedLoad());

  factory1_.monitor_->setPressure(0.65);
  timer_cb_();
  EXPECT_EQ(0, shed_load_count.value());
  EXPECT_FALSE(point->shouldShedLoad());

  factory1_.monitor_->setPressure(0.95);
  timer_cb_();
  EXPECT_TRUE(point->shouldShedLoad());
  EXPECT_EQ(1, shed_load_count.value());

  factory1_.monitor_->setPressure(0.85);
  timer_cb_();
  if (point->shouldShedLoad()) {
    EXPECT_EQ(2, shed_load_count.value());
  } else {
    EXPECT_EQ(1, shed_load_count.value());
  }
}

TEST_F(OverloadManagerLoadShedPointImplTest, PointWithMultipleTriggers) {
  setDispatcherExpectation();
  const std::string config = R"EOF(
    resource_monitors:
      - name: envoy.resource_monitors.fake_resource1
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: envoy.resource_monitors.fake_resource2
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Timestamp
    loadshed_points:
      - name: "test_point"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            threshold:
              value: 0.9
          - name: "envoy.resource_monitors.fake_resource2"
            scaled:
              scaling_threshold: 0.5
              saturation_threshold: 1.0
  )EOF";

  auto manager{createOverloadManager(config)};
  manager->start();

  LoadShedPoint* point = manager->getLoadShedPoint("test_point");
  ASSERT_NE(point, nullptr);

  Stats::Gauge& scale_percent =
      stats_.gauge("overload.test_point.scale_percent", Stats::Gauge::ImportMode::Accumulate);

  EXPECT_FALSE(point->shouldShedLoad());
  EXPECT_EQ(0, scale_percent.value());

  factory1_.monitor_->setPressure(0.95);
  timer_cb_();
  EXPECT_TRUE(point->shouldShedLoad());
  EXPECT_EQ(100, scale_percent.value());

  factory2_.monitor_->setPressure(0.75);
  timer_cb_();
  EXPECT_TRUE(point->shouldShedLoad());
  EXPECT_EQ(100, scale_percent.value());

  // shouldCheckLoad is now random since we're using a
  // scaling trigger that is now the highest activated trigger.
  // As such just check the scaling percent.
  factory1_.monitor_->setPressure(0.75);
  timer_cb_();
  EXPECT_EQ(50, scale_percent.value());

  factory2_.monitor_->setPressure(0.45);
  timer_cb_();
  EXPECT_FALSE(point->shouldShedLoad());
  EXPECT_EQ(0, scale_percent.value());
}

// Tests that compared to OverloadManagerActions that are posted with fixed
// OverloadState, the LoadShedPoint uses the most current reading.
TEST_F(OverloadManagerLoadShedPointImplTest, LoadShedPointShouldUseCurrentReading) {
  setDispatcherExpectation();
  const std::string config = R"EOF(
    resource_monitors:
      - name: envoy.resource_monitors.fake_resource1
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    loadshed_points:
      - name: "test_point"
        triggers:
          - name: "envoy.resource_monitors.fake_resource1"
            scaled:
              scaling_threshold: 0.5
              saturation_threshold: 1.0
    actions:
      - name: envoy.overload_actions.shrink_heap
        triggers:
          - name: envoy.resource_monitors.fake_resource1
            scaled:
              scaling_threshold: 0.5
              saturation_threshold: 1.0
  )EOF";

  auto manager{createOverloadManager(config)};

  Event::DispatcherPtr other_dispatcher{api_->allocateDispatcher("other_dispatcher")};
  std::vector<UnitFloat> overload_action_states;

  manager->registerForAction(
      "envoy.overload_actions.shrink_heap", *other_dispatcher,
      [&](OverloadActionState state) { overload_action_states.push_back(state.value()); });
  manager->start();

  LoadShedPoint* point = manager->getLoadShedPoint("test_point");
  ASSERT_NE(point, nullptr);

  factory1_.monitor_->setPressure(1.0f);
  other_dispatcher->post([&point]() { EXPECT_FALSE(point->shouldShedLoad()); });
  timer_cb_();

  // The pressure change should be propagated to the LoadShedPoint but not
  // the Overload Action.
  factory1_.monitor_->setPressure(0);
  other_dispatcher->post([&point]() { EXPECT_FALSE(point->shouldShedLoad()); });
  timer_cb_();

  other_dispatcher->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(overload_action_states[0], UnitFloat(1));
}

} // namespace
} // namespace Server
} // namespace Envoy
