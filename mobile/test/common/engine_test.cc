#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/common/engine.h"
#include "library/common/main_interface.h"

namespace Envoy {

class EngineTest : public testing::Test {};

typedef struct {
  absl::Notification on_engine_running;
  absl::Notification on_exit;
} engine_test_context;

TEST_F(EngineTest, EarlyExit) {
  const std::string config =
      "{\"admin\":{},\"static_resources\":{\"listeners\":[{\"name\":\"base_api_listener\","
      "\"address\":{\"socket_address\":{\"protocol\":\"TCP\",\"address\":\"0.0.0.0\",\"port_"
      "value\":10000}},\"api_listener\":{\"api_listener\":{\"@type\":\"type.googleapis.com/"
      "envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager\",\"stat_"
      "prefix\":\"hcm\",\"route_config\":{\"name\":\"api_router\",\"virtual_hosts\":[{\"name\":"
      "\"api\",\"include_attempt_count_in_response\":true,\"domains\":[\"*\"],\"routes\":[{"
      "\"match\":{\"prefix\":\"/"
      "\"},\"route\":{\"cluster_header\":\"x-envoy-mobile-cluster\",\"retry_policy\":{\"retry_back_"
      "off\":{\"base_interval\":\"0.25s\",\"max_interval\":\"60s\"}}}}]}]},\"http_filters\":[{"
      "\"name\":\"envoy.router\",\"typed_config\":{\"@type\":\"type.googleapis.com/"
      "envoy.extensions.filters.http.router.v3.Router\"}}]}}}]},\"layered_runtime\":{\"layers\":[{"
      "\"name\":\"static_layer_0\",\"static_layer\":{\"overload\":{\"global_downstream_max_"
      "connections\":50000}}}]}}";
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

  run_engine(0, callbacks, config.c_str(), level.c_str());
  ASSERT_TRUE(test_context.on_engine_running.WaitForNotificationWithTimeout(absl::Seconds(3)));

  terminate_engine(0);
  ASSERT_TRUE(test_context.on_exit.WaitForNotificationWithTimeout(absl::Seconds(3)));

  start_stream(0, {});
}
} // namespace Envoy
