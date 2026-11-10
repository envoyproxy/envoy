#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "source/extensions/resource_monitors/dynamic_modules/resource_monitor.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using testing::_;
using testing::HasSubstr;

class MockResourceUpdateCallbacks : public Server::ResourceUpdateCallbacks {
public:
  MOCK_METHOD(void, onSuccess, (const Server::ResourceUsage& usage));
  MOCK_METHOD(void, onFailure, (const absl::Status& error));
};

// The outcome that testOnUpdateResourceUsage returns, and the configurations it received.
envoy_dynamic_module_type_resource_monitor_update_status update_status;
double update_pressure;
envoy_dynamic_module_type_resource_monitor_config_envoy_ptr update_config_envoy_ptr;
envoy_dynamic_module_type_resource_monitor_config_module_ptr update_config_module_ptr;
// The scheduler that testOnUpdateResourceUsageReporting reports through from within the update
// hook.
envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr update_hook_scheduler;
// The number of destroy hook calls, the configuration that the last one received, and the
// scheduler that testOnConfigDestroy reports through when it is set.
int destroy_count;
envoy_dynamic_module_type_resource_monitor_config_module_ptr destroy_config_module_ptr;
envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr destroy_hook_scheduler;
// The pressure that testOnGetResourceUsage returns, the load shed points that testOnLoadAccepted
// received, and the configurations that the two hooks received.
double resource_usage;
std::vector<std::string> accepted_load_shed_points;
envoy_dynamic_module_type_resource_monitor_config_module_ptr get_resource_usage_config_module_ptr;
envoy_dynamic_module_type_resource_monitor_config_module_ptr load_accepted_config_module_ptr;

envoy_dynamic_module_type_resource_monitor_update_status testOnUpdateResourceUsage(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr,
    double* resource_pressure) {
  update_config_envoy_ptr = config_envoy_ptr;
  update_config_module_ptr = config_module_ptr;
  *resource_pressure = update_pressure;
  return update_status;
}

envoy_dynamic_module_type_resource_monitor_update_status testOnUpdateResourceUsageReporting(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr,
    double* resource_pressure) {
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(
      update_hook_scheduler, 0.4);
  return testOnUpdateResourceUsage(config_envoy_ptr, config_module_ptr, resource_pressure);
}

void testOnConfigDestroy(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr) {
  ++destroy_count;
  destroy_config_module_ptr = config_module_ptr;
  if (destroy_hook_scheduler != nullptr) {
    envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure(
        destroy_hook_scheduler);
  }
}

double testOnGetResourceUsage(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr) {
  get_resource_usage_config_module_ptr = config_module_ptr;
  return resource_usage;
}

void testOnLoadAccepted(
    envoy_dynamic_module_type_resource_monitor_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer load_shed_point_name) {
  load_accepted_config_module_ptr = config_module_ptr;
  accepted_load_shed_points.emplace_back(load_shed_point_name.ptr, load_shed_point_name.length);
}

class DynamicModuleResourceMonitorTest : public testing::Test {
public:
  void SetUp() override {
    update_status = envoy_dynamic_module_type_resource_monitor_update_status_Success;
    update_pressure = 0;
    update_config_envoy_ptr = nullptr;
    update_config_module_ptr = nullptr;
    update_hook_scheduler = nullptr;
    destroy_count = 0;
    destroy_config_module_ptr = nullptr;
    destroy_hook_scheduler = nullptr;

    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("resource_monitor_no_op", "c"),
        /*do_not_close=*/false);
    ASSERT_OK(dynamic_module.status());
    auto config = newDynamicModuleResourceMonitorConfig(
        "monitor", "config", std::move(dynamic_module.value()), dispatcher_);
    ASSERT_OK(config.status());
    config_ = std::move(config.value());
    config_->on_update_resource_usage_ = testOnUpdateResourceUsage;
    config_->on_config_destroy_ = testOnConfigDestroy;
    scheduler_ = envoy_dynamic_module_callback_resource_monitor_config_scheduler_new(
        static_cast<void*>(config_.get()));
  }

  void TearDown() override {
    envoy_dynamic_module_callback_resource_monitor_config_scheduler_delete(scheduler_);
  }

  void updateResourceUsage() {
    DynamicModuleResourceMonitor monitor(config_);
    monitor.updateResourceUsage(callbacks_);
  }

  void startPendingUpdate() {
    update_status = envoy_dynamic_module_type_resource_monitor_update_status_Pending;
    updateResourceUsage();
  }

  // Expects the next report to be posted to the main thread, and returns the posted callback.
  Event::PostCb& expectPost() {
    EXPECT_CALL(dispatcher_, post(_)).WillOnce([this](Event::PostCb callback) {
      posted_ = std::move(callback);
    });
    return posted_;
  }

  testing::StrictMock<Event::MockDispatcher> dispatcher_;
  testing::StrictMock<MockResourceUpdateCallbacks> callbacks_;
  DynamicModuleResourceMonitorConfigSharedPtr config_;
  envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr scheduler_{nullptr};
  Event::PostCb posted_;
};

TEST_F(DynamicModuleResourceMonitorTest, ReportsPressure) {
  update_pressure = 0.42;
  EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{0.42}));
  updateResourceUsage();
  EXPECT_EQ(static_cast<void*>(config_.get()), update_config_envoy_ptr);
  EXPECT_EQ(config_->in_module_config_, update_config_module_ptr);
}

TEST_F(DynamicModuleResourceMonitorTest, ReportsZeroPressure) {
  update_pressure = 0;
  EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{0}));
  updateResourceUsage();
}

TEST_F(DynamicModuleResourceMonitorTest, ReportsPressureAboveOneAsOne) {
  update_pressure = 3.5;
  EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{1}));
  updateResourceUsage();
}

TEST_F(DynamicModuleResourceMonitorTest, FailsUpdateWithInvalidPressure) {
  for (const double pressure :
       {-0.25, -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    SCOPED_TRACE(pressure);
    update_pressure = pressure;
    EXPECT_CALL(callbacks_, onFailure(HasStatus(absl::StatusCode::kInvalidArgument,
                                                HasSubstr("invalid resource pressure"))));
    updateResourceUsage();
    testing::Mock::VerifyAndClearExpectations(&callbacks_);
  }
}

TEST_F(DynamicModuleResourceMonitorTest, FailsUpdateWhenModuleFails) {
  update_status = envoy_dynamic_module_type_resource_monitor_update_status_Failure;
  EXPECT_CALL(callbacks_,
              onFailure(HasStatus(absl::StatusCode::kInternal, "the module reported a failure")));
  updateResourceUsage();
}

TEST_F(DynamicModuleResourceMonitorTest, FailsUpdateWithUnknownStatus) {
  // A value past the last enumerator stays within the range of the enum, which C++ requires.
  update_status = static_cast<envoy_dynamic_module_type_resource_monitor_update_status>(
      envoy_dynamic_module_type_resource_monitor_update_status_Pending + 1);
  EXPECT_CALL(callbacks_, onFailure(HasStatus(absl::StatusCode::kInternal,
                                              "the module returned an unknown update status")));
  updateResourceUsage();
}

TEST_F(DynamicModuleResourceMonitorTest, ReportsPendingUpdateOnMainThread) {
  startPendingUpdate();

  Event::PostCb& posted = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(scheduler_,
                                                                                        0.3);
  EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{0.3}));
  posted();

  // The update is no longer pending.
  Event::PostCb& posted_again = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure(scheduler_);
  posted_again();
}

TEST_F(DynamicModuleResourceMonitorTest, ReportsPendingPressureAboveOneAsOne) {
  startPendingUpdate();

  Event::PostCb& posted = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(scheduler_,
                                                                                        2.0);
  EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{1}));
  posted();
}

TEST_F(DynamicModuleResourceMonitorTest, FailsPendingUpdate) {
  startPendingUpdate();

  Event::PostCb& posted = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure(scheduler_);
  EXPECT_CALL(callbacks_,
              onFailure(HasStatus(absl::StatusCode::kInternal, "the module reported a failure")));
  posted();

  // The update is no longer pending.
  Event::PostCb& posted_again = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(scheduler_,
                                                                                        0.5);
  posted_again();
}

TEST_F(DynamicModuleResourceMonitorTest, FailsPendingUpdateWithInvalidPressure) {
  startPendingUpdate();

  Event::PostCb& posted = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(
      scheduler_, std::numeric_limits<double>::quiet_NaN());
  EXPECT_CALL(callbacks_, onFailure(HasStatus(absl::StatusCode::kInvalidArgument,
                                              HasSubstr("invalid resource pressure"))));
  posted();
}

TEST_F(DynamicModuleResourceMonitorTest, DropsReportWithoutPendingUpdate) {
  Event::PostCb& posted = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(scheduler_,
                                                                                        0.5);
  posted();
}

TEST_F(DynamicModuleResourceMonitorTest, ReportFromUpdateHookAppliesToItsPendingUpdate) {
  config_->on_update_resource_usage_ = testOnUpdateResourceUsageReporting;
  update_hook_scheduler = scheduler_;
  Event::PostCb& posted = expectPost();
  startPendingUpdate();

  EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{0.4}));
  posted();
}

TEST_F(DynamicModuleResourceMonitorTest, ModuleThreadReportsPendingUpdate) {
  startPendingUpdate();

  Event::PostCb& posted = expectPost();
  std::thread module_thread([this] {
    envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(
        scheduler_, 0.6);
  });
  module_thread.join();

  EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{0.6}));
  posted();
}

TEST_F(DynamicModuleResourceMonitorTest, DestroyDropsPendingUpdate) {
  startPendingUpdate();

  // The destroy hook reports through the scheduler, which no longer reaches the configuration.
  destroy_hook_scheduler = scheduler_;
  const auto in_module_config = config_->in_module_config_;
  config_.reset();
  EXPECT_EQ(1, destroy_count);
  EXPECT_EQ(in_module_config, destroy_config_module_ptr);
}

TEST_F(DynamicModuleResourceMonitorTest, DestroyWithoutInModuleConfigSkipsDestroyHook) {
  // A module that failed to create its configuration has nothing to destroy.
  config_->in_module_config_ = nullptr;
  config_.reset();
  EXPECT_EQ(0, destroy_count);
}

TEST_F(DynamicModuleResourceMonitorTest, SchedulerDropsReportsAfterDestroy) {
  // The scheduler does not keep the configuration alive.
  config_.reset();
  EXPECT_EQ(1, destroy_count);

  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(scheduler_,
                                                                                        0.5);
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure(scheduler_);
}

TEST_F(DynamicModuleResourceMonitorTest, DropsReportPostedBeforeDestroy) {
  startPendingUpdate();

  Event::PostCb& posted = expectPost();
  envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure(scheduler_);
  config_.reset();
  posted();
}

TEST_F(DynamicModuleResourceMonitorTest, EnableSynchronousFeedbackAfterCreationFails) {
  EXPECT_FALSE(envoy_dynamic_module_callback_resource_monitor_config_enable_synchronous_feedback(
      static_cast<void*>(config_.get())));
  EXPECT_FALSE(config_->synchronous_feedback_enabled_);
}

TEST_F(DynamicModuleResourceMonitorTest, EnableSynchronousFeedbackOffMainThreadIsEnvoyBug) {
  // The module may enable synchronous feedback while it creates its configuration, so a call off
  // the main thread must fail even then. In builds where an Envoy bug aborts, only the bug message
  // is checked.
  config_->in_config_new_ = true;
  EXPECT_ENVOY_BUG(
      {
        std::thread thread([this] {
          EXPECT_FALSE(
              envoy_dynamic_module_callback_resource_monitor_config_enable_synchronous_feedback(
                  static_cast<void*>(config_.get())));
        });
        thread.join();
      },
      "envoy_dynamic_module_callback_resource_monitor_config_enable_synchronous_feedback must be "
      "called on the main thread");
  EXPECT_FALSE(config_->synchronous_feedback_enabled_);
}

class DynamicModuleSynchronousFeedbackResourceMonitorTest : public testing::Test {
public:
  void SetUp() override {
    resource_usage = 0;
    accepted_load_shed_points.clear();
    get_resource_usage_config_module_ptr = nullptr;
    load_accepted_config_module_ptr = nullptr;

    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("resource_monitor_synchronous_feedback",
                                                         "c"),
        /*do_not_close=*/false);
    ASSERT_OK(dynamic_module.status());
    auto config = newDynamicModuleResourceMonitorConfig(
        "monitor", "config", std::move(dynamic_module.value()), dispatcher_);
    ASSERT_OK(config.status());
    config_ = std::move(config.value());
    config_->on_get_resource_usage_ = testOnGetResourceUsage;
    config_->on_load_accepted_ = testOnLoadAccepted;
    monitor_ = std::make_unique<DynamicModuleSynchronousFeedbackResourceMonitor>(config_);
  }

  testing::StrictMock<Event::MockDispatcher> dispatcher_;
  testing::StrictMock<MockResourceUpdateCallbacks> callbacks_;
  DynamicModuleResourceMonitorConfigSharedPtr config_;
  std::unique_ptr<DynamicModuleSynchronousFeedbackResourceMonitor> monitor_;
};

// The pressures that the module reports, and the pressures that Envoy acts on.
std::vector<std::pair<double, double>> clampedPressures() {
  const double infinity = std::numeric_limits<double>::infinity();
  return {{0.3, 0.3},    {0, 0},    {1, 1},         {2, 1},
          {infinity, 1}, {-0.5, 0}, {-infinity, 0}, {std::numeric_limits<double>::quiet_NaN(), 0}};
}

TEST_F(DynamicModuleSynchronousFeedbackResourceMonitorTest, ReadsLivePressure) {
  for (const auto& [pressure, expected] : clampedPressures()) {
    SCOPED_TRACE(pressure);
    resource_usage = pressure;
    EXPECT_EQ(Server::ResourceUsage{expected}, monitor_->getResourceUsage());
    // The refresh reads the same pressure.
    EXPECT_CALL(callbacks_, onSuccess(Server::ResourceUsage{expected}));
    monitor_->updateResourceUsage(callbacks_);
    testing::Mock::VerifyAndClearExpectations(&callbacks_);
  }
  EXPECT_EQ(config_->in_module_config_, get_resource_usage_config_module_ptr);
}

TEST_F(DynamicModuleSynchronousFeedbackResourceMonitorTest, PassesAcceptedLoad) {
  monitor_->onLoadAccepted("envoy.load_shed_points.tcp_listener_accept");
  EXPECT_EQ(std::vector<std::string>{"envoy.load_shed_points.tcp_listener_accept"},
            accepted_load_shed_points);
  EXPECT_EQ(config_->in_module_config_, load_accepted_config_module_ptr);
}

TEST_F(DynamicModuleSynchronousFeedbackResourceMonitorTest, IgnoresAcceptedLoadWithoutHook) {
  config_->on_load_accepted_ = nullptr;
  monitor_->onLoadAccepted("envoy.load_shed_points.tcp_listener_accept");
  EXPECT_TRUE(accepted_load_shed_points.empty());
}

} // namespace
} // namespace DynamicModules
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
