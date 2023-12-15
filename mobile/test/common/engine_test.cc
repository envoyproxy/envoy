#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/engine.h"
#include "library/common/main_interface.h"

namespace Envoy {

// RAII wrapper for the engine, ensuring that we properly shut down the engine. If the engine
// thread is not torn down, we end up with TSAN failures during shutdown due to a data race
// between the main thread and the engine thread both writing to the
// Envoy::Logger::current_log_context global.
struct TestEngineHandle {
  envoy_engine_t handle_;
  TestEngineHandle(envoy_engine_callbacks callbacks, const std::string& level) {
    handle_ = init_engine(callbacks, {}, {});
    Platform::EngineBuilder builder;
    auto bootstrap = builder.generateBootstrap();
    std::string yaml = Envoy::MessageUtil::getYamlStringFromMessage(*bootstrap);
    run_engine(handle_, yaml.c_str(), level.c_str());
  }

  envoy_status_t terminate() { return terminate_engine(handle_, /* release */ false); }

  ~TestEngineHandle() { terminate_engine(handle_, /* release */ true); }
};

class EngineTest : public testing::Test {
public:
  std::unique_ptr<TestEngineHandle> engine_;
};

typedef struct {
  absl::Notification on_engine_running;
  absl::Notification on_exit;
} engine_test_context;

TEST_F(EngineTest, EarlyExit) {
  const std::string level = "debug";

  engine_test_context test_context{};
  envoy_engine_callbacks callbacks{[](void* context) -> void {
                                     auto* engine_running =
                                         static_cast<engine_test_context*>(context);
                                     engine_running->on_engine_running.Notify();
                                   } /*on_engine_running*/,
                                   [](void* context) -> void {
                                     auto* exit = static_cast<engine_test_context*>(context);
                                     exit->on_exit.Notify();
                                   } /*on_exit*/,
                                   &test_context /*context*/};

  engine_ = std::make_unique<TestEngineHandle>(callbacks, level);
  envoy_engine_t handle = engine_->handle_;
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  ASSERT_EQ(engine_->terminate(), ENVOY_SUCCESS);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));

  start_stream(handle, 0, {}, false);

  engine_.reset();
}

TEST_F(EngineTest, AccessEngineAfterInitialization) {
  const std::string level = "debug";

  engine_test_context test_context{};
  envoy_engine_callbacks callbacks{[](void* context) -> void {
                                     auto* engine_running =
                                         static_cast<engine_test_context*>(context);
                                     engine_running->on_engine_running.Notify();
                                   } /*on_engine_running*/,
                                   [](void*) -> void {} /*on_exit*/, &test_context /*context*/};

  engine_ = std::make_unique<TestEngineHandle>(callbacks, level);
  envoy_engine_t handle = engine_->handle_;
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification getClusterManagerInvoked;
  envoy_data stats_data;
  // Running engine functions should work because the engine is running
  EXPECT_EQ(ENVOY_SUCCESS, dump_stats(handle, &stats_data));
  release_envoy_data(stats_data);

  engine_->terminate();

  // Now that the engine has been shut down, we no longer expect scheduling to work.
  EXPECT_EQ(ENVOY_FAILURE, dump_stats(handle, &stats_data));

  engine_.reset();
}

} // namespace Envoy
