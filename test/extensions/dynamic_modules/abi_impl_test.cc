#include "source/extensions/dynamic_modules/abi.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

// Test that the weak symbol stub for scheduler_new triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SchedulerNewEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(nullptr); },
      "not implemented in this context");
}

// Test that the weak symbol stub for scheduler_delete triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SchedulerDeleteEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(nullptr); },
      "not implemented in this context");
}

// Test that the weak symbol stub for scheduler_commit triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SchedulerCommitEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(nullptr, 0); },
      "not implemented in this context");
}

// Test that the weak symbol stub for http_callout triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, HttpCalloutEnvoyBug) {
  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"cluster", 7};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
            nullptr, &callout_id, cluster_name, nullptr, 0, body, 5000);
        EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_counter_value triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, GetCounterValueEnvoyBug) {
  uint64_t value = 0;
  envoy_dynamic_module_type_module_buffer name = {"counter_name", 12};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
            nullptr, name, &value);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_gauge_value triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, GetGaugeValueEnvoyBug) {
  uint64_t value = 0;
  envoy_dynamic_module_type_module_buffer name = {"gauge_name", 10};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
            nullptr, name, &value);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_histogram_summary triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, GetHistogramSummaryEnvoyBug) {
  uint64_t sample_count = 0;
  double sample_sum = 0.0;
  envoy_dynamic_module_type_module_buffer name = {"histogram_name", 14};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(
            nullptr, name, &sample_count, &sample_sum);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for iterate_counters triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, IterateCountersEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(nullptr, nullptr,
                                                                           nullptr);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for iterate_gauges triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, IterateGaugesEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(nullptr, nullptr, nullptr);
      },
      "not implemented in this context");
}

// ---------------------- Upstream HTTP-TCP bridge weak stub tests ------------------------

// Test that the weak symbol stub for get_request_headers_count triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, GetRequestHeadersCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result =
            envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_count(
                nullptr);
        EXPECT_EQ(result, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_request_header triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, GetRequestHeaderEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer key = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
            nullptr, 0, &key, &value);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_request_header_value triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, GetRequestHeaderValueEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto result =
            envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header_value(
                nullptr, key, &value);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_upstream_buffer triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, GetUpstreamBufferEnvoyBug) {
  uintptr_t buffer_ptr = 0;
  size_t length = 0;
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_upstream_buffer(
            nullptr, &buffer_ptr, &length);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for set_upstream_buffer triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, SetUpstreamBufferEnvoyBug) {
  envoy_dynamic_module_type_module_buffer data = {"data", 4};
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_upstream_buffer(nullptr, data);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for append_upstream_buffer triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, AppendUpstreamBufferEnvoyBug) {
  envoy_dynamic_module_type_module_buffer data = {"data", 4};
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_upstream_buffer(nullptr,
                                                                                      data);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_downstream_buffer triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, GetDownstreamBufferEnvoyBug) {
  uintptr_t buffer_ptr = 0;
  size_t length = 0;
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_downstream_buffer(
            nullptr, &buffer_ptr, &length);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for drain_downstream_buffer triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, DrainDownstreamBufferEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_upstream_http_tcp_bridge_drain_downstream_buffer(nullptr, 10);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for set_response_header triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, SetResponseHeaderEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_module_buffer value = {"value", 5};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(
            nullptr, key, value);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for add_response_header triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, AddResponseHeaderEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_module_buffer value = {"value", 5};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(
            nullptr, key, value);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for send_response triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, SendResponseEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(nullptr, false); },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_route_name triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, GetRouteNameEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_route_name(nullptr, &result); },
      "not implemented in this context");
}

// Test that the weak symbol stub for get_cluster_name triggers an ENVOY_BUG when called.
TEST(UpstreamHttpTcpBridgeAbiImplTest, GetClusterNameEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_cluster_name(nullptr, &result);
      },
      "not implemented in this context");
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
