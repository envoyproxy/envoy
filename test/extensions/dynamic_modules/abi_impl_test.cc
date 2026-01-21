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

// =====================================================================
// Load Balancer weak symbol stub tests
// =====================================================================

// Test that the weak symbol stub for lb_get_cluster_name triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetClusterNameEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_lb_get_cluster_name(nullptr, &result); },
      "not implemented in this context");
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);
}

// Test that the weak symbol stub for lb_get_hosts_count triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostsCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_get_hosts_count(nullptr, 0);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_healthy_hosts_count triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHealthyHostsCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_get_healthy_hosts_count(nullptr, 0);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_degraded_hosts_count triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetDegradedHostsCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_get_degraded_hosts_count(nullptr, 0);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_priority_set_size triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetPrioritySetSizeEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto size = envoy_dynamic_module_callback_lb_get_priority_set_size(nullptr);
        EXPECT_EQ(size, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_address triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostAddressEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_lb_get_host_address(nullptr, 0, 0, &result);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);
}

// Test that the weak symbol stub for lb_get_host_weight triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostWeightEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto weight = envoy_dynamic_module_callback_lb_get_host_weight(nullptr, 0, 0);
        EXPECT_EQ(weight, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_health triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostHealthEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto health = envoy_dynamic_module_callback_lb_get_host_health(nullptr, 0, 0);
        EXPECT_EQ(health, envoy_dynamic_module_type_host_health_Unhealthy);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_context_compute_hash_key triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbContextComputeHashKeyEnvoyBug) {
  uint64_t hash_out = 0;
  EXPECT_ENVOY_BUG(
      {
        auto success =
            envoy_dynamic_module_callback_lb_context_compute_hash_key(nullptr, &hash_out);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_context_get_downstream_headers_count triggers an ENVOY_BUG
// when called.
TEST(CommonAbiImplTest, LbContextGetDownstreamHeadersCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_context_get_downstream_headers_count(nullptr);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_context_get_downstream_header triggers an ENVOY_BUG when
// called.
TEST(CommonAbiImplTest, LbContextGetDownstreamHeaderEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer key = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_lb_context_get_downstream_header(nullptr, 0,
                                                                                      &key, &value);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_context_get_downstream_header_value triggers an ENVOY_BUG
// when called.
TEST(CommonAbiImplTest, LbContextGetDownstreamHeaderValueEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"test", 4};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_lb_context_get_downstream_header_value(
            nullptr, key, &value);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
