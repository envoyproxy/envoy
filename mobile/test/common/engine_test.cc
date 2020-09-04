#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/common/engine.h"
#include "library/common/main_interface.h"

namespace Envoy {

class EngineTest : public testing::Test {};

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
  absl::Notification done;
  envoy_engine_callbacks cbs{[](void* context) -> void {
                               auto* done = static_cast<absl::Notification*>(context);
                               done->Notify();
                             },
                             &done};

  run_engine(0, cbs, config.c_str(), level.c_str());

  terminate_engine(0);

  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(1)));

  start_stream(0, {});
}
} // namespace Envoy
