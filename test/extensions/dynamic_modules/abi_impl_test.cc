#include "source/extensions/dynamic_modules/abi/abi.h"

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
// Function registry tests
// =====================================================================

// Test registering and retrieving a function.
TEST(CommonAbiImplTest, FunctionRegistryRegisterAndGet) {
  auto fn = [](int x) { return x + 1; };
  envoy_dynamic_module_type_module_buffer key = {"fn_basic", 8};

  EXPECT_TRUE(envoy_dynamic_module_callback_register_function(key, reinterpret_cast<void*>(+fn)));

  void* fn_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_function(key, &fn_out));
  EXPECT_NE(fn_out, nullptr);

  // Cast back and call the function.
  auto resolved = reinterpret_cast<int (*)(int)>(fn_out);
  EXPECT_EQ(resolved(41), 42);
}

// Test that getting a non-existent key returns false.
TEST(CommonAbiImplTest, FunctionRegistryGetNonExistent) {
  envoy_dynamic_module_type_module_buffer key = {"fn_nonexistent", 14};
  void* fn_out = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_get_function(key, &fn_out));
  EXPECT_EQ(fn_out, nullptr);
}

// Test that registering nullptr returns false.
TEST(CommonAbiImplTest, FunctionRegistryRegisterNull) {
  envoy_dynamic_module_type_module_buffer key = {"fn_null", 7};
  EXPECT_FALSE(envoy_dynamic_module_callback_register_function(key, nullptr));

  // Key should not exist in the registry.
  void* fn_out = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_get_function(key, &fn_out));
}

// Test that duplicate registration returns false.
TEST(CommonAbiImplTest, FunctionRegistryDuplicateRegistration) {
  auto fn1 = [](int x) { return x; };
  auto fn2 = [](int x) { return x * 2; };
  envoy_dynamic_module_type_module_buffer key = {"fn_dup", 6};

  EXPECT_TRUE(envoy_dynamic_module_callback_register_function(key, reinterpret_cast<void*>(+fn1)));

  // Second registration under the same key should fail.
  EXPECT_FALSE(envoy_dynamic_module_callback_register_function(key, reinterpret_cast<void*>(+fn2)));

  // The original function should still be registered.
  void* fn_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_function(key, &fn_out));
  auto resolved = reinterpret_cast<int (*)(int)>(fn_out);
  EXPECT_EQ(resolved(5), 5);
}

// Test multiple independent keys.
TEST(CommonAbiImplTest, FunctionRegistryMultipleKeys) {
  auto fn_a = [](int x) { return x + 10; };
  auto fn_b = [](int x) { return x + 20; };
  envoy_dynamic_module_type_module_buffer key_a = {"fn_multi_a", 10};
  envoy_dynamic_module_type_module_buffer key_b = {"fn_multi_b", 10};

  EXPECT_TRUE(
      envoy_dynamic_module_callback_register_function(key_a, reinterpret_cast<void*>(+fn_a)));
  EXPECT_TRUE(
      envoy_dynamic_module_callback_register_function(key_b, reinterpret_cast<void*>(+fn_b)));

  void* out_a = nullptr;
  void* out_b = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_function(key_a, &out_a));
  EXPECT_TRUE(envoy_dynamic_module_callback_get_function(key_b, &out_b));

  auto resolved_a = reinterpret_cast<int (*)(int)>(out_a);
  auto resolved_b = reinterpret_cast<int (*)(int)>(out_b);
  EXPECT_EQ(resolved_a(0), 10);
  EXPECT_EQ(resolved_b(0), 20);
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

// Test that the weak symbol stub for lb_get_healthy_host_address triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHealthyHostAddressEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto success =
            envoy_dynamic_module_callback_lb_get_healthy_host_address(nullptr, 0, 0, &result);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);
}

// Test that the weak symbol stub for lb_get_healthy_host_weight triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHealthyHostWeightEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto weight = envoy_dynamic_module_callback_lb_get_healthy_host_weight(nullptr, 0, 0);
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

// Test that the weak symbol stub for lb_context_get_downstream_headers_size triggers an ENVOY_BUG
// when called.
TEST(CommonAbiImplTest, LbContextGetDownstreamHeadersSizeEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(nullptr);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_context_get_downstream_headers triggers an ENVOY_BUG
// when called.
TEST(CommonAbiImplTest, LbContextGetDownstreamHeadersEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto success =
            envoy_dynamic_module_callback_lb_context_get_downstream_headers(nullptr, nullptr);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_context_get_downstream_header triggers an ENVOY_BUG
// when called.
TEST(CommonAbiImplTest, LbContextGetDownstreamHeaderEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"test-key", 8};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_lb_context_get_downstream_header(
            nullptr, key, &result, 0, nullptr);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
