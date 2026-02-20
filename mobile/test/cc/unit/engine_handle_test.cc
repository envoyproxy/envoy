#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/internal_engine.h"

namespace Envoy {
namespace Platform {
namespace {

TEST(EngineHandleTest, CreateFromHandleIsNonOwning) {
  absl::Notification engine_running;
  EngineBuilder builder;
  builder.enableLogger(false).setOnEngineRunning([&engine_running]() { engine_running.Notify(); });

  EngineSharedPtr owning_engine = builder.build();
  engine_running.WaitForNotification();

  const int64_t handle = owning_engine->getInternalEngineHandle();
  {
    EngineSharedPtr handle_engine = Engine::createFromInternalEngineHandle(handle);
    EXPECT_EQ(handle, handle_engine->getInternalEngineHandle());
  }

  EXPECT_FALSE(owning_engine->engine()->isTerminated());
  EXPECT_EQ(ENVOY_SUCCESS, owning_engine->terminate());
  EXPECT_TRUE(owning_engine->engine()->isTerminated());
}

} // namespace
} // namespace Platform
} // namespace Envoy
