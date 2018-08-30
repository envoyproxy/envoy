#include "common/event/dispatcher_impl.h"

#include "server/resource_monitor_config_impl.h"

#include "extensions/resource_monitors/injected_resource/injected_resource_monitor.h"

#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Property;

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

class TestableInjectedResourceMonitor : public InjectedResourceMonitor {
public:
  TestableInjectedResourceMonitor(
      const envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig&
          config,
      Server::Configuration::ResourceMonitorFactoryContext& context)
      : InjectedResourceMonitor(config, context), dispatcher_(context.dispatcher()) {}

protected:
  void onFileChanged() override {
    InjectedResourceMonitor::onFileChanged();
    dispatcher_.exit();
  }

private:
  Event::Dispatcher& dispatcher_;
};

class MockedCallbacks : public Server::ResourceMonitor::Callbacks {
public:
  MOCK_METHOD1(onSuccess, void(const Server::ResourceUsage&));
  MOCK_METHOD1(onFailure, void(const EnvoyException&));
};

class InjectedResourceMonitorTest : public testing::Test {
protected:
  InjectedResourceMonitorTest()
      : dispatcher_(test_time_.timeSource()),
        resource_filename_(TestEnvironment::temporaryPath("injected_resource")),
        file_updater_(resource_filename_) {}

  void updateResource(const std::string& contents) { file_updater_.update(contents); }

  void updateResource(double pressure) { updateResource(absl::StrCat(pressure)); }

  std::unique_ptr<InjectedResourceMonitor> createMonitor() {
    envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig config;
    config.set_filename(resource_filename_);
    Server::Configuration::ResourceMonitorFactoryContextImpl context(dispatcher_);
    return std::make_unique<TestableInjectedResourceMonitor>(config, context);
  }

  DangerousDeprecatedTestTime test_time_;
  Event::DispatcherImpl dispatcher_;
  const std::string resource_filename_;
  AtomicFileUpdater file_updater_;
  MockedCallbacks cb_;
};

TEST_F(InjectedResourceMonitorTest, ReportsCorrectPressure) {
  auto monitor(createMonitor());

  updateResource(0.6);
  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_CALL(cb_, onSuccess(Server::ResourceUsage{.resource_pressure_ = 0.6}));
  monitor->updateResourceUsage(cb_);

  updateResource(0.7);
  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_CALL(cb_, onSuccess(Server::ResourceUsage{.resource_pressure_ = 0.7}));
  monitor->updateResourceUsage(cb_);
}

MATCHER_P(ExceptionContains, rhs, "") { return absl::StrContains(arg.what(), rhs); }

TEST_F(InjectedResourceMonitorTest, ReportsParseError) {
  auto monitor(createMonitor());

  updateResource("bad content");
  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_CALL(cb_, onFailure(ExceptionContains("failed to parse injected resource pressure")));
  monitor->updateResourceUsage(cb_);
}

TEST_F(InjectedResourceMonitorTest, ReportsErrorForOutOfRangePressure) {
  auto monitor(createMonitor());

  updateResource(-1);
  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_CALL(cb_, onFailure(ExceptionContains("pressure out of range")));
  monitor->updateResourceUsage(cb_);

  updateResource(2);
  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_CALL(cb_, onFailure(ExceptionContains("pressure out of range")));
  monitor->updateResourceUsage(cb_);
}

TEST_F(InjectedResourceMonitorTest, ReportsErrorOnFileRead) {
  auto monitor(createMonitor());

  EXPECT_CALL(cb_, onFailure(ExceptionContains("unable to read file")));
  monitor->updateResourceUsage(cb_);
}

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
