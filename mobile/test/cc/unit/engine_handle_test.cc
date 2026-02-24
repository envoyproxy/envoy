#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/internal_engine.h"

namespace Envoy {
namespace Platform {
namespace {

TEST(EngineHandleTest, CreateFromHandleIsNotHandlingTermination) {
  absl::Notification engine_running;
  EngineBuilder builder;
  builder.enableLogger(false).setOnEngineRunning([&engine_running]() { engine_running.Notify(); });

  EngineSharedPtr owning_engine = builder.build();
  engine_running.WaitForNotification();

  const int64_t handle = owning_engine->getInternalEngineHandle();
  {
    auto handle_engine_or = Engine::createFromInternalEngineHandle(handle);
    ASSERT_TRUE(handle_engine_or.ok()) << handle_engine_or.status();
    EngineSharedPtr handle_engine = handle_engine_or.value();
    EXPECT_EQ(handle, handle_engine->getInternalEngineHandle());
  }

  EXPECT_FALSE(owning_engine->engine()->isTerminated());
  EXPECT_EQ(ENVOY_SUCCESS, owning_engine->terminate());
  EXPECT_TRUE(owning_engine->engine()->isTerminated());
}

TEST(EngineHandleTest, CreateFromNullHandleReturnsError) {
  auto handle_engine_or = Engine::createFromInternalEngineHandle(0);
  ASSERT_FALSE(handle_engine_or.ok());
  EXPECT_EQ(handle_engine_or.status().message(), "Invalid internal engine handle.");
}

} // namespace
} // namespace Platform
} // namespace Envoy
