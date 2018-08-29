#include <cstdint>
#include <fstream>

#include "common/event/dispatcher_impl.h"

#include "server/resource_monitor_config_impl.h"

#include "extensions/resource_monitors/injected_resource/injected_resource_monitor.h"

#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"

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
  InjectedResourceMonitorTest() : dispatcher_(test_time_.timeSource()) {}

  void SetUp() override {
    injected_resource_target1_ = TestEnvironment::temporaryPath("envoy_test/injected_resource1");
    injected_resource_target2_ = TestEnvironment::temporaryPath("envoy_test/injected_resource2");
    injected_resource_link_ = TestEnvironment::temporaryPath("envoy_test/injected_resource");
    injected_resource_new_link_ =
        TestEnvironment::temporaryPath("envoy_test/injected_resource_tmp");

    unlink(injected_resource_target1_.c_str());
    unlink(injected_resource_target2_.c_str());
    unlink(injected_resource_link_.c_str());
    unlink(injected_resource_new_link_.c_str());
    mkdir(TestEnvironment::temporaryPath("envoy_test").c_str(), S_IRWXU);

    use_target1_ = true;
  }

  void updateResource(const std::string& contents) {
    const std::string target =
        use_target1_ ? injected_resource_target1_ : injected_resource_target2_;
    use_target1_ = !use_target1_;
    {
      std::ofstream file(target);
      file << contents;
    }
    int rc = symlink(target.c_str(), injected_resource_new_link_.c_str());
    EXPECT_EQ(0, rc) << strerror(errno);
    rc = rename(injected_resource_new_link_.c_str(), injected_resource_link_.c_str());
    EXPECT_EQ(0, rc) << strerror(errno);
  }

  void updateResource(double pressure) { updateResource(absl::StrCat(pressure)); }

  std::unique_ptr<InjectedResourceMonitor> createMonitor() {
    envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig config;
    config.set_filename(injected_resource_link_);
    Server::Configuration::ResourceMonitorFactoryContextImpl context(dispatcher_);
    return std::make_unique<TestableInjectedResourceMonitor>(config, context);
  }

  DangerousDeprecatedTestTime test_time_;
  Event::DispatcherImpl dispatcher_;
  bool use_target1_;
  std::string injected_resource_target1_;
  std::string injected_resource_target2_;
  std::string injected_resource_link_;
  std::string injected_resource_new_link_;
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

TEST_F(InjectedResourceMonitorTest, ErrorOnParseError) {
  auto monitor(createMonitor());

  updateResource("bad content");
  dispatcher_.run(Event::Dispatcher::RunType::Block);
  EXPECT_CALL(cb_, onFailure(ExceptionContains("failed to parse injected resource pressure")));
  monitor->updateResourceUsage(cb_);
}

TEST_F(InjectedResourceMonitorTest, ErrorOnFileRead) {
  auto monitor(createMonitor());

  EXPECT_CALL(cb_, onFailure(ExceptionContains("unable to read file")));
  monitor->updateResourceUsage(cb_);
}

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
