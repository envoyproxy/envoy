#include <filesystem>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/metadata.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/health_checkers/dynamic_modules/health_checker.h"
#include "source/extensions/health_checkers/dynamic_modules/health_checker_config.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_check_event_logger.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {
namespace {

// Drives the real Rust test module end-to-end through a real dispatcher and verifies that the
// status the module posts back (from its own thread) is applied to the host's health.
class DynamicModuleHealthCheckerTest : public testing::Test {
public:
  DynamicModuleHealthCheckerTest()
      : cluster_(new NiceMock<Upstream::MockClusterMockPrioritySet>()),
        event_logger_(new NiceMock<Upstream::MockHealthCheckEventLogger>()),
        api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  // Builds a checker that loads the named module and dispatches to the named in-module checker with
  // the given config bytes.
  void buildChecker(const std::string& module_name, const std::string& checker_name,
                    const std::string& config) {
    const std::string yaml = R"EOF(
    timeout: 0.5s
    interval: 0.1s
    no_traffic_interval: 0.1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.dynamic_modules
    )EOF";
    const auto health_check_config = Upstream::parseHealthCheckFromV3Yaml(yaml);

    auto module = Extensions::DynamicModules::newDynamicModuleByName(module_name, true, false);
    ASSERT_OK(module);
    auto config_or_error =
        newDynamicModuleHealthCheckerConfig(checker_name, config, std::move(module.value()));
    ASSERT_OK(config_or_error);

    health_checker_ = std::make_shared<DynamicModuleHealthChecker>(
        *cluster_, health_check_config, std::move(config_or_error.value()), *dispatcher_, runtime_,
        random_, Upstream::HealthCheckEventLoggerPtr(event_logger_));
  }

  // Builds the checker for the given Rust-module mode ("healthy"/"degraded"/"unhealthy"/"timeout"/
  // "double"); the mode is passed as the in-module config.
  void setup(const std::string& mode) {
    buildChecker("health_checker_integration_test", "test_health_checker", mode);
  }

  // Points the module search path at the C test_data directory (used for C-only modules).
  void useCModulePath() {
    const std::string so =
        Extensions::DynamicModules::testSharedObjectPath("health_checker_no_op", "c");
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               std::filesystem::path(so).parent_path().string(), 1);
  }

  // A host with endpoint metadata under the ``envoy.lb`` namespace so the module's metadata
  // accessors exercise the lookup callbacks.
  Upstream::HostSharedPtr hostWithMetadata() {
    envoy::config::core::v3::Metadata metadata;
    Config::Metadata::mutableMetadataValue(metadata, "envoy.lb", "str_key").set_string_value("v");
    Config::Metadata::mutableMetadataValue(metadata, "envoy.lb", "num_key").set_number_value(1.0);
    Config::Metadata::mutableMetadataValue(metadata, "envoy.lb", "bool_key").set_bool_value(true);
    return Upstream::makeTestHostWithMetadata(
        cluster_->info_, std::make_shared<const envoy::config::core::v3::Metadata>(metadata),
        "tcp://127.0.0.1:80");
  }

  void setHosts(const Upstream::HostSharedPtr& host) {
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  }

  // Runs the dispatcher until the first health check result is delivered for the host (proving the
  // module reported back across threads, or the timeout path fired), or a wall-clock fallback
  // fires. Returns whether a result was delivered.
  bool runUntilFirstResult(std::chrono::milliseconds fallback = std::chrono::seconds(15)) {
    bool done = false;
    health_checker_->addHostCheckCompleteCb(
        [&](const Upstream::HostSharedPtr&, Upstream::HealthTransition, Upstream::HealthState) {
          done = true;
          dispatcher_->exit();
        });

    auto timeout = dispatcher_->createTimer([this]() { dispatcher_->exit(); });
    timeout->enableTimer(fallback);

    health_checker_->start();
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    return done;
  }

  std::shared_ptr<Upstream::MockClusterMockPrioritySet> cluster_;
  Upstream::MockHealthCheckEventLogger* event_logger_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<DynamicModuleHealthChecker> health_checker_;
};

TEST_F(DynamicModuleHealthCheckerTest, ReportsHealthy) {
  setup("healthy");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

TEST_F(DynamicModuleHealthCheckerTest, ReportsDegraded) {
  setup("degraded");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
}

TEST_F(DynamicModuleHealthCheckerTest, ReportsUnhealthy) {
  setup("unhealthy");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

// The module never reports; Envoy's timeout fires and records an active health check failure,
// exercising the session's onTimeout path.
TEST_F(DynamicModuleHealthCheckerTest, TimesOut) {
  setup("timeout");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

// The module reports twice; the second result arrives after the check resolved and is ignored.
TEST_F(DynamicModuleHealthCheckerTest, IgnoresSecondReport) {
  setup("double");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
  // Drain the queued second report so reportResult()'s already-resolved guard executes.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

// Exercises the host info accessors (address, metadata string/number/bool, health) on a host that
// carries endpoint metadata, including wrong-typed and missing lookups.
TEST_F(DynamicModuleHealthCheckerTest, ReadsHostMetadata) {
  setup("healthy");
  setHosts(hostWithMetadata());

  EXPECT_TRUE(runUntilFirstResult());
}

// A host whose address and metadata are absent exercises the accessors' empty/not-found fallbacks
// (e.g. get_host_address returns false when the host has no resolved address).
TEST_F(DynamicModuleHealthCheckerTest, ReadsHostWithoutAddressOrMetadata) {
  setup("healthy");
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
}

// The module reads the host health while the host is degraded, exercising that branch of the
// get_host_health callback.
TEST_F(DynamicModuleHealthCheckerTest, ReadsDegradedHostHealth) {
  setup("healthy");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
}

// The module reads the host health while the host is unhealthy, exercising that branch of the
// get_host_health callback.
TEST_F(DynamicModuleHealthCheckerTest, ReadsUnhealthyHostHealth) {
  setup("healthy");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
}

// A module whose session_new returns null yields a session with no in-module counterpart:
// on_interval and on_timeout skip the module (their null-session guards), but Envoy still drives
// the interval/timeout and records a timeout failure.
TEST_F(DynamicModuleHealthCheckerTest, SessionNewNull) {
  useCModulePath();
  buildChecker("health_checker_session_new_null", "test", "");
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  setHosts(host);

  EXPECT_TRUE(runUntilFirstResult());
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

// A report whose session was already torn down (receiver cleared) while the checker is still alive
// is a no-op: the callback runs on the main thread and skips the null receiver.
TEST_F(DynamicModuleHealthCheckerTest, ReportAfterSessionGone) {
  setup("healthy");
  struct StubReceiver : public HealthCheckResultReceiver {
    void reportResult(envoy_dynamic_module_type_host_health) override { called = true; }
    bool called{false};
  } receiver;
  auto control_block = std::make_shared<DynamicModuleHealthCheckSessionControlBlock>(
      std::weak_ptr<Upstream::HealthCheckerImplBase>(health_checker_), receiver);
  control_block->receiver_ = nullptr; // session torn down before the report lands
  DynamicModuleHealthCheckerScheduler scheduler(control_block);
  scheduler.report(envoy_dynamic_module_type_host_health_Healthy);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(receiver.called);
}

} // namespace
} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
