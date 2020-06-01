#include "envoy/config/overload/v3/overload.pb.h"
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
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Server {
namespace {

class FakeResourceMonitor : public ResourceMonitor {
public:
  FakeResourceMonitor(Event::Dispatcher& dispatcher)
      : success_(true), pressure_(0), error_("fake error"), dispatcher_(dispatcher) {}

  void setPressure(double pressure) {
    success_ = true;
    pressure_ = pressure;
  }

  void setError() { success_ = false; }

  void updateResourceUsage(ResourceMonitor::Callbacks& callbacks) override {
    if (success_) {
      Server::ResourceUsage usage;
      usage.resource_pressure_ = pressure_;
      dispatcher_.post([&, usage]() { callbacks.onSuccess(usage); });
    } else {
      EnvoyException& error = error_;
      dispatcher_.post([&, error]() { callbacks.onFailure(error); });
    }
  }

private:
  bool success_;
  double pressure_;
  EnvoyException error_;
  Event::Dispatcher& dispatcher_;
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
        factory2_("envoy.resource_monitors.fake_resource2"), register_factory1_(factory1_),
        register_factory2_(factory2_), api_(Api::createApiForTest(stats_)) {}

  void setDispatcherExpectation() {
    timer_ = new NiceMock<Event::MockTimer>();
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([&](Event::TimerCb cb) {
      timer_cb_ = cb;
      return timer_;
    }));
  }

  envoy::config::overload::v3::OverloadManager parseConfig(const std::string& config) {
    envoy::config::overload::v3::OverloadManager proto;
    bool success = Protobuf::TextFormat::ParseFromString(config, &proto);
    ASSERT(success);
    return proto;
  }

  std::string getConfig() {
    return R"EOF(
      refresh_interval {
        seconds: 1
      }
      resource_monitors {
        name: "envoy.resource_monitors.fake_resource1"
      }
      resource_monitors {
        name: "envoy.resource_monitors.fake_resource2"
      }
      actions {
        name: "envoy.overload_actions.dummy_action"
        triggers {
          name: "envoy.resource_monitors.fake_resource1"
          threshold {
            value: 0.9
          }
        }
        triggers {
          name: "envoy.resource_monitors.fake_resource2"
          threshold {
            value: 0.8
          }
        }
      }
    )EOF";
  }

  std::string getConfigSimple() {
    return R"EOF(
      refresh_interval {
        seconds: 1
      }
      resource_monitors {
        name: "envoy.resource_monitors.fake_resource1"
      }
      actions {
        name: "envoy.overload_actions.dummy_action"
        triggers {
          name: "envoy.resource_monitors.fake_resource1"
          threshold {
            value: 0.9
          }
        }
      }
    )EOF";
  }

  std::unique_ptr<OverloadManagerImpl> createOverloadManager(const std::string& config) {
    return std::make_unique<OverloadManagerImpl>(dispatcher_, stats_, thread_local_,
                                                 parseConfig(config), validation_visitor_, *api_);
  }

  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Struct> factory1_;
  FakeResourceMonitorFactory<Envoy::ProtobufWkt::Timestamp> factory2_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory1_;
  Registry::InjectFactory<Configuration::ResourceMonitorFactory> register_factory2_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Event::MockTimer>* timer_; // not owned
  Stats::TestUtil::TestStore stats_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Event::TimerCb timer_cb_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

TEST_F(OverloadManagerImplTest, CallbackOnlyFiresWhenStateChanges) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(getConfig()));
  bool is_active = false;
  int cb_count = 0;
  manager->registerForAction("envoy.overload_actions.dummy_action", dispatcher_,
                             [&](OverloadActionState state) {
                               is_active = state == OverloadActionState::Active;
                               cb_count++;
                             });
  manager->registerForAction("envoy.overload_actions.unknown_action", dispatcher_,
                             [&](OverloadActionState) { EXPECT_TRUE(false); });
  manager->start();

  Stats::Gauge& active_gauge = stats_.gauge("overload.envoy.overload_actions.dummy_action.active",
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
  EXPECT_EQ(action_state, OverloadActionState::Inactive);
  EXPECT_EQ(0, cb_count);
  EXPECT_EQ(0, active_gauge.value());
  EXPECT_EQ(50, pressure_gauge1.value());

  // Update exceeds fake_resource1 trigger threshold, callback is expected
  factory1_.monitor_->setPressure(0.95);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(action_state, OverloadActionState::Active);
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(1, active_gauge.value());
  EXPECT_EQ(95, pressure_gauge1.value());

  // Callback should not be invoked if action state does not change
  factory1_.monitor_->setPressure(0.94);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(action_state, OverloadActionState::Active);
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(94, pressure_gauge1.value());

  // The action is already active for fake_resource1 so no callback expected
  factory2_.monitor_->setPressure(0.9);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(action_state, OverloadActionState::Active);
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(90, pressure_gauge2.value());

  // The action remains active for fake_resource2 so no callback expected
  factory1_.monitor_->setPressure(0.5);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(action_state, OverloadActionState::Active);
  EXPECT_EQ(1, cb_count);
  EXPECT_EQ(50, pressure_gauge1.value());
  EXPECT_EQ(90, pressure_gauge2.value());

  // Both become inactive so callback is expected
  factory2_.monitor_->setPressure(0.3);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_EQ(action_state, OverloadActionState::Inactive);
  EXPECT_EQ(2, cb_count);
  EXPECT_EQ(30, pressure_gauge2.value());

  // Different triggers, both become active, only one callback expected
  factory1_.monitor_->setPressure(0.97);
  factory2_.monitor_->setPressure(0.96);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(action_state, OverloadActionState::Active);
  EXPECT_EQ(3, cb_count);
  EXPECT_EQ(97, pressure_gauge1.value());
  EXPECT_EQ(96, pressure_gauge2.value());

  // Different triggers, both become inactive, only one callback expected
  factory1_.monitor_->setPressure(0.41);
  factory2_.monitor_->setPressure(0.42);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_EQ(action_state, OverloadActionState::Inactive);
  EXPECT_EQ(4, cb_count);
  EXPECT_EQ(41, pressure_gauge1.value());
  EXPECT_EQ(42, pressure_gauge2.value());

  manager->stop();
}

TEST_F(OverloadManagerImplTest, FailedUpdates) {
  setDispatcherExpectation();
  auto manager(createOverloadManager(getConfig()));
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

TEST_F(OverloadManagerImplTest, SkippedUpdates) {
  setDispatcherExpectation();

  // Save the post callback instead of executing it.
  // Note that this test works for only one resource. If using the default config,
  // two events fire, so a list of all post_cb's between timer_cb_'s would need to be invoked.
  Event::PostCb post_cb;
  ON_CALL(dispatcher_, post(_)).WillByDefault(Invoke([&](Event::PostCb cb) { post_cb = cb; }));

  auto manager(createOverloadManager(getConfigSimple()));
  manager->start();
  Stats::Counter& skipped_updates =
      stats_.counter("overload.envoy.resource_monitors.fake_resource1.skipped_updates");

  timer_cb_();
  EXPECT_EQ(0, skipped_updates.value());
  timer_cb_();
  EXPECT_EQ(1, skipped_updates.value());
  timer_cb_();
  EXPECT_EQ(2, skipped_updates.value());
  post_cb();
  timer_cb_();
  EXPECT_EQ(2, skipped_updates.value());

  manager->stop();
}

TEST_F(OverloadManagerImplTest, DuplicateResourceMonitor) {
  const std::string config = R"EOF(
    resource_monitors {
      name: "envoy.resource_monitors.fake_resource1"
    }
    resource_monitors {
      name: "envoy.resource_monitors.fake_resource1"
    }
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate resource monitor .*");
}

TEST_F(OverloadManagerImplTest, DuplicateOverloadAction) {
  const std::string config = R"EOF(
    actions {
      name: "envoy.overload_actions.dummy_action"
    }
    actions {
      name: "envoy.overload_actions.dummy_action"
    }
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Duplicate overload action .*");
}

TEST_F(OverloadManagerImplTest, UnknownTrigger) {
  const std::string config = R"EOF(
    actions {
      name: "envoy.overload_actions.dummy_action"
      triggers {
        name: "envoy.resource_monitors.fake_resource1"
        threshold {
          value: 0.9
        }
      }
    }
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException,
                          "Unknown trigger resource .*");
}

TEST_F(OverloadManagerImplTest, DuplicateTrigger) {
  const std::string config = R"EOF(
    resource_monitors {
      name: "envoy.resource_monitors.fake_resource1"
    }
    actions {
      name: "envoy.overload_actions.dummy_action"
      triggers {
        name: "envoy.resource_monitors.fake_resource1"
        threshold {
          value: 0.9
        }
      }
      triggers {
        name: "envoy.resource_monitors.fake_resource1"
        threshold {
          value: 0.8
        }
      }
    }
  )EOF";

  EXPECT_THROW_WITH_REGEX(createOverloadManager(config), EnvoyException, "Duplicate trigger .*");
}

TEST_F(OverloadManagerImplTest, Shutdown) {
  setDispatcherExpectation();

  auto manager(createOverloadManager(getConfig()));
  manager->start();

  EXPECT_CALL(*timer_, disableTimer());
  manager->stop();
}

} // namespace
} // namespace Server
} // namespace Envoy
