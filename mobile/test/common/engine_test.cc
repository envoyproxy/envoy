#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/common/config/templates.h"
#include "library/common/engine.h"
#include "library/common/engine_handle.h"
#include "library/common/main_interface.h"

namespace Envoy {

// This config is the minimal envoy mobile config that allows for running the engine.
const std::string MINIMAL_TEST_CONFIG = R"(
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
            - name: api
              include_attempt_count_in_response: true
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                route:
                  cluster_header: x-envoy-mobile-cluster
                  retry_policy:
                    retry_back_off: { base_interval: 0.25s, max_interval: 60s }
          http_filters:
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
layered_runtime:
  layers:
  - name: static_layer_0
    static_layer:
      overload: { global_downstream_max_connections: 50000 }
)";

// RAII wrapper for the engine, ensuring that we properly shut down the engine. If the engine
// thread is not torn down, we end up with TSAN failures during shutdown due to a data race
// between the main thread and the engine thread both writing to the
// Envoy::Logger::current_log_context global.
struct TestEngineHandle {
  envoy_engine_t handle_;
  TestEngineHandle(envoy_engine_callbacks callbacks, const std::string& level) {
    handle_ = init_engine(callbacks, {}, {});
    run_engine(handle_, MINIMAL_TEST_CONFIG.c_str(), level.c_str());
  }

  void terminate() { terminate_engine(handle_); }
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
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  engine_->terminate();
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));

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
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  absl::Notification getClusterManagerInvoked;
  // Scheduling on the dispatcher should work, the engine is running.
  EXPECT_EQ(ENVOY_SUCCESS, EngineHandle::runOnEngineDispatcher(
                               handle, [&getClusterManagerInvoked](Envoy::Engine& engine) {
                                 engine.getClusterManager();
                                 getClusterManagerInvoked.Notify();
                               }));

  // Validate that we actually invoked the function.
  EXPECT_TRUE(getClusterManagerInvoked.WaitForNotificationWithTimeout(absl::Seconds(1)));

  engine_->terminate();

  // Now that the engine has been shut down, we no longer expect scheduling to work.
  EXPECT_EQ(ENVOY_FAILURE, EngineHandle::runOnEngineDispatcher(
                               handle, [](Envoy::Engine& engine) { engine.getClusterManager(); }));

  engine_.reset();
}

} // namespace Envoy
