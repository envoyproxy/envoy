#include "common/event/dispatcher_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "server/resource_monitor_config_impl.h"

#include "extensions/resource_monitors/injected_resource/injected_resource_monitor.h"

#include "test/test_common/environment.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

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
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher()),
        resource_filename_(TestEnvironment::temporaryPath("injected_resource")),
        file_updater_(resource_filename_), monitor_(createMonitor()) {}

  void updateResource(const std::string& contents) {
    file_updater_.update(contents);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    monitor_->updateResourceUsage(cb_);
  }

  void updateResource(double pressure) { updateResource(absl::StrCat(pressure)); }

  std::unique_ptr<InjectedResourceMonitor> createMonitor() {
    envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig config;
    config.set_filename(resource_filename_);
    Server::Configuration::ResourceMonitorFactoryContextImpl context(*dispatcher_, *api_);
    return std::make_unique<TestableInjectedResourceMonitor>(config, context);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  const std::string resource_filename_;
  AtomicFileUpdater file_updater_;
  MockedCallbacks cb_;
  std::unique_ptr<InjectedResourceMonitor> monitor_;
};

TEST_F(InjectedResourceMonitorTest, ReportsCorrectPressure) {
  EXPECT_CALL(cb_, onSuccess(Server::ResourceUsage{0.6}));
  updateResource(0.6);

  EXPECT_CALL(cb_, onSuccess(Server::ResourceUsage{0.7}));
  updateResource(0.7);
}

MATCHER_P(ExceptionContains, rhs, "") { return absl::StrContains(arg.what(), rhs); }

TEST_F(InjectedResourceMonitorTest, ReportsParseError) {
  EXPECT_CALL(cb_, onFailure(ExceptionContains("failed to parse injected resource pressure")));
  updateResource("bad content");
}

TEST_F(InjectedResourceMonitorTest, ReportsErrorForOutOfRangePressure) {
  EXPECT_CALL(cb_, onFailure(ExceptionContains("pressure out of range")));
  updateResource(-1);

  EXPECT_CALL(cb_, onFailure(ExceptionContains("pressure out of range")));
  updateResource(2);
}

TEST_F(InjectedResourceMonitorTest, ReportsErrorOnFileRead) {
  EXPECT_CALL(cb_, onFailure(ExceptionContains("Invalid path")));
  monitor_->updateResourceUsage(cb_);
}

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
