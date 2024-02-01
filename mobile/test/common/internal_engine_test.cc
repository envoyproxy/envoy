#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/internal_engine.h"

namespace Envoy {

// RAII wrapper for the engine, ensuring that we properly shut down the engine. If the engine
// thread is not torn down, we end up with TSAN failures during shutdown due to a data race
// between the main thread and the engine thread both writing to the
// Envoy::Logger::current_log_context global.
struct TestEngine {
  std::unique_ptr<InternalEngine> engine_;
  envoy_engine_t handle() { return reinterpret_cast<envoy_engine_t>(engine_.get()); }
  TestEngine(envoy_engine_callbacks callbacks, const std::string& level) {
    engine_.reset(new Envoy::InternalEngine(callbacks, {}, {}));
    Platform::EngineBuilder builder;
    auto bootstrap = builder.generateBootstrap();
    std::string yaml = Envoy::MessageUtil::getYamlStringFromMessage(*bootstrap);
    engine_->run(yaml.c_str(), level.c_str());
  }

  envoy_status_t terminate() { return engine_->terminate(); }
  bool isTerminated() const { return engine_->isTerminated(); }

  ~TestEngine() {
    if (!engine_->isTerminated()) {
      engine_->terminate();
    }
  }
};

class EngineTest : public testing::Test {
public:
  std::unique_ptr<TestEngine> engine_;
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

  engine_ = std::make_unique<TestEngine>(callbacks, level);
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  ASSERT_EQ(engine_->terminate(), ENVOY_SUCCESS);
  ASSERT_TRUE(engine_->isTerminated());
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(10)));

  engine_->engine_->startStream(0, {}, false);

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

  engine_ = std::make_unique<TestEngine>(callbacks, level);
  engine_->handle();
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(10)));

  absl::Notification getClusterManagerInvoked;
  // Running engine functions should work because the engine is running
  EXPECT_EQ("runtime.load_success: 1\n", engine_->engine_->dumpStats());

  engine_->terminate();
  ASSERT_TRUE(engine_->isTerminated());

  // Now that the engine has been shut down, we no longer expect scheduling to work.
  EXPECT_EQ("", engine_->engine_->dumpStats());

  engine_.reset();
}

} // namespace Envoy
