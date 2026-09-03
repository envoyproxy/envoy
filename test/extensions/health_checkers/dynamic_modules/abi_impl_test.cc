#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/health_checkers/dynamic_modules/health_checker.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {
namespace {

// The strong ABI callbacks must be safe no-ops when given a null Envoy pointer / output.
TEST(DynamicModuleHealthCheckerAbiImplTest, NullArgumentsAreSafe) {
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_health_checker_scheduler_new(nullptr));
  // No crash for a null scheduler.
  envoy_dynamic_module_callback_health_checker_scheduler_report(
      nullptr, envoy_dynamic_module_type_host_health_Healthy);
  envoy_dynamic_module_callback_health_checker_scheduler_delete(nullptr);

  envoy_dynamic_module_type_envoy_buffer buf{};
  EXPECT_FALSE(envoy_dynamic_module_callback_health_checker_get_host_address(nullptr, &buf));
  // Null output is rejected before the session pointer is touched.
  EXPECT_FALSE(envoy_dynamic_module_callback_health_checker_get_host_address(
      reinterpret_cast<envoy_dynamic_module_type_health_checker_session_envoy_ptr>(0x1), nullptr));

  envoy_dynamic_module_type_module_buffer ns{"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer key{"k", 1};
  double num = 0;
  bool flag = false;
  const auto fake_session =
      reinterpret_cast<envoy_dynamic_module_type_health_checker_session_envoy_ptr>(0x1);
  EXPECT_FALSE(envoy_dynamic_module_callback_health_checker_get_host_metadata_string(nullptr, ns,
                                                                                     key, &buf));
  EXPECT_FALSE(envoy_dynamic_module_callback_health_checker_get_host_metadata_number(nullptr, ns,
                                                                                     key, &num));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_health_checker_get_host_metadata_bool(nullptr, ns, key, &flag));
  // A null output buffer is rejected before the session pointer is touched.
  EXPECT_FALSE(envoy_dynamic_module_callback_health_checker_get_host_metadata_string(
      fake_session, ns, key, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_health_checker_get_host_metadata_number(
      fake_session, ns, key, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_health_checker_get_host_metadata_bool(fake_session, ns,
                                                                                   key, nullptr));
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy,
            envoy_dynamic_module_callback_health_checker_get_host_health(nullptr));
}

struct StubReceiver : public HealthCheckResultReceiver {
  void reportResult(envoy_dynamic_module_type_host_health) override { called = true; }
  bool called{false};
};

// When the owning checker's weak_ptr has expired, reporting is a safe no-op: the dispatcher is gone
// too, so nothing is posted and the receiver is never invoked.
TEST(DynamicModuleHealthCheckerAbiImplTest, ReportAfterCheckerGoneIsNoop) {
  StubReceiver receiver;
  auto control_block = std::make_shared<DynamicModuleHealthCheckSessionControlBlock>(
      std::weak_ptr<Upstream::HealthCheckerImplBase>(), receiver);
  DynamicModuleHealthCheckerScheduler scheduler(control_block);
  scheduler.report(envoy_dynamic_module_type_host_health_Healthy);
  EXPECT_FALSE(receiver.called);
}

} // namespace
} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
