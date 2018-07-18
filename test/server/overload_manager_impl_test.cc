#include "envoy/registry/registry.h"
#include "envoy/server/resource_monitor.h"
#include "envoy/server/resource_monitor_config.h"

#include "server/overload_manager_impl.h"

#include "extensions/resource_monitors/common/factory_base.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Server {

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

class FakeResourceMonitorFactory : public Extensions::ResourceMonitors::Common::FactoryBase<
                                       envoy::config::overload::v2alpha::EmptyConfig> {
public:
  FakeResourceMonitorFactory(const std::string& name) : FactoryBase(name), monitor_(nullptr) {}

  ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const envoy::config::overload::v2alpha::EmptyConfig&,
      Server::Configuration::ResourceMonitorFactoryContext& context) override {
    ASSERT(!monitor_);
    auto monitor = std::make_unique<FakeResourceMonitor>(context.dispatcher());
    monitor_ = monitor.get();
    return std::move(monitor);
  }

  FakeResourceMonitor* monitor_; // not owned
};

class OverloadManagerImplTest : public testing::Test {
protected:
  OverloadManagerImplTest()
      : factory1_("envoy.resource_monitors.fake_resource1"),
        factory2_("envoy.resource_monitors.fake_resource2") {
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([&](Event::TimerCb cb) {
      timer_cb_ = cb;
      return new NiceMock<Event::MockTimer>();
    }));

    Registry::FactoryRegistry<Configuration::ResourceMonitorFactory>::registerFactory(factory1_);
    Registry::FactoryRegistry<Configuration::ResourceMonitorFactory>::registerFactory(factory2_);
  }

  envoy::config::overload::v2alpha::OverloadManager getConfig() {
    const std::string config = R"EOF(
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

    envoy::config::overload::v2alpha::OverloadManager proto;
    ASSERT(Protobuf::TextFormat::ParseFromString(config, &proto));
    return proto;
  }

  FakeResourceMonitorFactory factory1_;
  FakeResourceMonitorFactory factory2_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::TimerCb timer_cb_;
};

TEST_F(OverloadManagerImplTest, CallbackOnlyFiresWhenStateChanges) {
  OverloadManagerImpl manager(dispatcher_, getConfig());
  bool is_active = false;
  int cb_count = 0;
  manager.registerForAction("envoy.overload_actions.dummy_action", dispatcher_, [&](bool value) {
    is_active = value;
    cb_count++;
  });
  manager.registerForAction("envoy.overload_actions.unknown_action", dispatcher_,
                            [&](bool) { ASSERT(false); });
  manager.start();

  factory1_.monitor_->setPressure(0.5);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_EQ(0, cb_count);

  factory1_.monitor_->setPressure(0.95);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(1, cb_count);

  // Callback should not be invoked if action active state has not changed
  factory1_.monitor_->setPressure(0.94);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(1, cb_count);

  // Different triggers firing but overall action remains active so no callback expected
  factory1_.monitor_->setPressure(0.5);
  factory2_.monitor_->setPressure(0.9);
  timer_cb_();
  EXPECT_TRUE(is_active);
  EXPECT_EQ(1, cb_count);

  factory2_.monitor_->setPressure(0.4);
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_EQ(2, cb_count);

  factory1_.monitor_->setPressure(0.95);
  factory1_.monitor_->setError();
  timer_cb_();
  EXPECT_FALSE(is_active);
  EXPECT_EQ(2, cb_count);
}

} // namespace Server
} // namespace Envoy
