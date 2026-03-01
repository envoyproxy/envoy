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

// Test that the weak symbol stub for signal_init_complete triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SignalInitCompleteEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(nullptr); },
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

// Test that the weak symbol stub for timer_new triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, TimerNewEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_timer_new(nullptr);
        EXPECT_EQ(result, nullptr);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for timer_enable triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, TimerEnableEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_timer_enable(nullptr, 100); },
      "not implemented in this context");
}

// Test that the weak symbol stub for timer_disable triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, TimerDisableEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_timer_disable(nullptr); },
      "not implemented in this context");
}

// Test that the weak symbol stub for timer_enabled triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, TimerEnabledEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(nullptr);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for timer_delete triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, TimerDeleteEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_timer_delete(nullptr); },
      "not implemented in this context");
}

// =====================================================================
// Bootstrap extension admin handler weak symbol stub tests
// =====================================================================

// Test that the weak symbol stub for register_admin_handler triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, RegisterAdminHandlerEnvoyBug) {
  envoy_dynamic_module_type_module_buffer path = {"/test", 5};
  envoy_dynamic_module_type_module_buffer help = {"help", 4};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
            nullptr, path, help, true, false);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for remove_admin_handler triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, RemoveAdminHandlerEnvoyBug) {
  envoy_dynamic_module_type_module_buffer path = {"/test", 5};
  EXPECT_ENVOY_BUG(
      {
        auto result =
            envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(nullptr, path);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for admin_set_response triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, AdminSetResponseEnvoyBug) {
  envoy_dynamic_module_type_module_buffer body = {"response", 8};
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(nullptr, body); },
      "not implemented in this context");
}

// =====================================================================
// Bootstrap extension stats definition and update weak symbol stub tests
// =====================================================================

// Test that the weak symbol stub for define_counter triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, DefineCounterEnvoyBug) {
  size_t counter_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"counter", 7};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
            nullptr, name, nullptr, 0, &counter_id);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for increment_counter triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, IncrementCounterEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
            nullptr, 0, nullptr, 0, 1);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for define_gauge triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, DefineGaugeEnvoyBug) {
  size_t gauge_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"gauge", 5};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
            nullptr, name, nullptr, 0, &gauge_id);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for set_gauge triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SetGaugeEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
            nullptr, 0, nullptr, 0, 42);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for increment_gauge triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, IncrementGaugeEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
            nullptr, 0, nullptr, 0, 1);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for decrement_gauge triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, DecrementGaugeEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
            nullptr, 0, nullptr, 0, 1);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for define_histogram triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, DefineHistogramEnvoyBug) {
  size_t histogram_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"histogram", 9};
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
            nullptr, name, nullptr, 0, &histogram_id);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for record_histogram_value triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, RecordHistogramValueEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result =
            envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
                nullptr, 0, nullptr, 0, 100);
        EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
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
// Cert Validator weak symbol stub tests
// =====================================================================

// Test that the weak symbol stub for cert_validator_set_error_details triggers an ENVOY_BUG when
// called.
TEST(CommonAbiImplTest, CertValidatorSetErrorDetailsEnvoyBug) {
  envoy_dynamic_module_type_module_buffer error_details = {"error", 5};
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_cert_validator_set_error_details(nullptr, error_details); },
      "not implemented in this context");
}

// Test that the weak symbol stub for cert_validator_set_filter_state triggers an ENVOY_BUG when
// called.
TEST(CommonAbiImplTest, CertValidatorSetFilterStateEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_module_buffer value = {"value", 5};
  EXPECT_ENVOY_BUG(
      {
        auto result =
            envoy_dynamic_module_callback_cert_validator_set_filter_state(nullptr, key, value);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for cert_validator_get_filter_state triggers an ENVOY_BUG when
// called.
TEST(CommonAbiImplTest, CertValidatorGetFilterStateEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"key", 3};
  envoy_dynamic_module_type_envoy_buffer value_out = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto result =
            envoy_dynamic_module_callback_cert_validator_get_filter_state(nullptr, key, &value_out);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// =====================================================================
// Cluster extension weak symbol stub tests
// =====================================================================

// Test that the weak symbol stub for cluster_add_hosts triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, ClusterAddHostsEnvoyBug) {
  envoy_dynamic_module_type_module_buffer addr = {"127.0.0.1:80", 12};
  uint32_t weight = 1;
  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptr = nullptr;
  EXPECT_ENVOY_BUG(
      {
        auto result =
            envoy_dynamic_module_callback_cluster_add_hosts(nullptr, &addr, &weight, 1, &host_ptr);
        EXPECT_FALSE(result);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for cluster_remove_hosts triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, ClusterRemoveHostsEnvoyBug) {
  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptr = nullptr;
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_cluster_remove_hosts(nullptr, &host_ptr, 1);
        EXPECT_EQ(result, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for cluster_pre_init_complete triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, ClusterPreInitCompleteEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_cluster_pre_init_complete(nullptr); },
      "not implemented in this context");
}

// Test that the weak symbol stub for cluster_lb_get_healthy_host_count triggers an ENVOY_BUG.
TEST(CommonAbiImplTest, ClusterLbGetHealthyHostCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(nullptr, 0);
        EXPECT_EQ(result, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for cluster_lb_get_healthy_host triggers an ENVOY_BUG.
TEST(CommonAbiImplTest, ClusterLbGetHealthyHostEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto result = envoy_dynamic_module_callback_cluster_lb_get_healthy_host(nullptr, 0, 0);
        EXPECT_EQ(result, nullptr);
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

// Test that the weak symbol stub for lb_get_host_address triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostAddressEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto found = envoy_dynamic_module_callback_lb_get_host_address(nullptr, 0, 0, &result);
        EXPECT_FALSE(found);
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

// Test that the weak symbol stub for lb_get_host_active_requests triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostActiveRequestsEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_get_host_active_requests(nullptr, 0, 0);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_active_connections triggers an ENVOY_BUG when
// called.
TEST(CommonAbiImplTest, LbGetHostActiveConnectionsEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_get_host_active_connections(nullptr, 0, 0);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_locality triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostLocalityEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto found = envoy_dynamic_module_callback_lb_get_host_locality(nullptr, 0, 0, nullptr,
                                                                        nullptr, nullptr);
        EXPECT_FALSE(found);
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

// Test that the weak symbol stub for lb_set_host_data triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbSetHostDataEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_lb_set_host_data(nullptr, 0, 0, 42);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_data triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostDataEnvoyBug) {
  uintptr_t data = 0;
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_lb_get_host_data(nullptr, 0, 0, &data);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_metadata_string triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostMetadataStringEnvoyBug) {
  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer key = {"version", 7};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto found = envoy_dynamic_module_callback_lb_get_host_metadata_string(
            nullptr, 0, 0, filter_name, key, &result);
        EXPECT_FALSE(found);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_metadata_number triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostMetadataNumberEnvoyBug) {
  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer key = {"version", 7};
  double result = 0.0;
  EXPECT_ENVOY_BUG(
      {
        auto found = envoy_dynamic_module_callback_lb_get_host_metadata_number(
            nullptr, 0, 0, filter_name, key, &result);
        EXPECT_FALSE(found);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_host_metadata_bool triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetHostMetadataBoolEnvoyBug) {
  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer key = {"version", 7};
  bool result = false;
  EXPECT_ENVOY_BUG(
      {
        auto found = envoy_dynamic_module_callback_lb_get_host_metadata_bool(
            nullptr, 0, 0, filter_name, key, &result);
        EXPECT_FALSE(found);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_locality_count triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetLocalityCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_get_locality_count(nullptr, 0);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_locality_host_count triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetLocalityHostCountEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_lb_get_locality_host_count(nullptr, 0, 0);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_locality_host_address triggers an ENVOY_BUG when
// called.
TEST(CommonAbiImplTest, LbGetLocalityHostAddressEnvoyBug) {
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto found =
            envoy_dynamic_module_callback_lb_get_locality_host_address(nullptr, 0, 0, 0, &result);
        EXPECT_FALSE(found);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for lb_get_locality_weight triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, LbGetLocalityWeightEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto weight = envoy_dynamic_module_callback_lb_get_locality_weight(nullptr, 0, 0);
        EXPECT_EQ(weight, 0);
      },
      "not implemented in this context");
}

// =====================================================================
// Matcher weak symbol stub tests
// =====================================================================

// Test that the weak symbol stub for matcher_get_headers_size triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, MatcherGetHeadersSizeEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto count = envoy_dynamic_module_callback_matcher_get_headers_size(
            nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader);
        EXPECT_EQ(count, 0);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for matcher_get_headers triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, MatcherGetHeadersEnvoyBug) {
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_matcher_get_headers(
            nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader, nullptr);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

// Test that the weak symbol stub for matcher_get_header_value triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, MatcherGetHeaderValueEnvoyBug) {
  envoy_dynamic_module_type_module_buffer key = {"test-key", 8};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_ENVOY_BUG(
      {
        auto success = envoy_dynamic_module_callback_matcher_get_header_value(
            nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 0,
            nullptr);
        EXPECT_FALSE(success);
      },
      "not implemented in this context");
}

// =============================================================================
// Weak symbol stub tests for network filter, listener filter, access logger, and
// UDP listener filter callbacks. These verify that the weak stubs installed in
// abi_impl.cc trigger ENVOY_BUG when called from a context that does not compile
// in the corresponding filter type.
// =============================================================================

// Macro to reduce copy-paste: verify each weak stub triggers ENVOY_BUG.
#define WEAK_STUB(TestSuffix, call)                                                                \
  TEST(CommonAbiImplTest, TestSuffix##EnvoyBug) {                                                  \
    EXPECT_ENVOY_BUG({ call; }, "not implemented in this context");                                \
  }

WEAK_STUB(NetworkFilterWrite,
          envoy_dynamic_module_callback_network_filter_write(nullptr, {nullptr, 0}, false))
WEAK_STUB(NetworkFilterInjectReadData,
          envoy_dynamic_module_callback_network_filter_inject_read_data(nullptr, {nullptr, 0},
                                                                        false))
WEAK_STUB(NetworkFilterInjectWriteData,
          envoy_dynamic_module_callback_network_filter_inject_write_data(nullptr, {nullptr, 0},
                                                                         false))
WEAK_STUB(NetworkFilterContinueReading,
          envoy_dynamic_module_callback_network_filter_continue_reading(nullptr))
WEAK_STUB(NetworkFilterClose,
          envoy_dynamic_module_callback_network_filter_close(
              nullptr, envoy_dynamic_module_type_network_connection_close_type_FlushWrite))
WEAK_STUB(NetworkFilterDisableClose,
          envoy_dynamic_module_callback_network_filter_disable_close(nullptr, false))
WEAK_STUB(NetworkFilterCloseWithDetails,
          envoy_dynamic_module_callback_network_filter_close_with_details(
              nullptr, envoy_dynamic_module_type_network_connection_close_type_FlushWrite,
              {nullptr, 0}))
WEAK_STUB(NetworkSetDynamicMetadataString,
          envoy_dynamic_module_callback_network_set_dynamic_metadata_string(nullptr, {nullptr, 0},
                                                                            {nullptr, 0},
                                                                            {nullptr, 0}))
WEAK_STUB(NetworkSetDynamicMetadataNumber,
          envoy_dynamic_module_callback_network_set_dynamic_metadata_number(nullptr, {nullptr, 0},
                                                                            {nullptr, 0}, 0))
WEAK_STUB(NetworkFilterEnableHalfClose,
          envoy_dynamic_module_callback_network_filter_enable_half_close(nullptr, false))
WEAK_STUB(NetworkFilterSetBufferLimits,
          envoy_dynamic_module_callback_network_filter_set_buffer_limits(nullptr, 0))
WEAK_STUB(NetworkFilterSchedulerCommit,
          envoy_dynamic_module_callback_network_filter_scheduler_commit(nullptr, 0))
WEAK_STUB(NetworkFilterSchedulerDelete,
          envoy_dynamic_module_callback_network_filter_scheduler_delete(nullptr))
WEAK_STUB(NetworkFilterConfigSchedulerDelete,
          envoy_dynamic_module_callback_network_filter_config_scheduler_delete(nullptr))
WEAK_STUB(NetworkFilterConfigSchedulerCommit,
          envoy_dynamic_module_callback_network_filter_config_scheduler_commit(nullptr, 0))
WEAK_STUB(NetworkSetSocketOptionInt,
          envoy_dynamic_module_callback_network_set_socket_option_int(
              nullptr, 0, 0, envoy_dynamic_module_type_socket_option_state_Prebind, 0))
WEAK_STUB(NetworkSetSocketOptionBytes,
          envoy_dynamic_module_callback_network_set_socket_option_bytes(
              nullptr, 0, 0, envoy_dynamic_module_type_socket_option_state_Prebind, {nullptr, 0}))
WEAK_STUB(NetworkGetSocketOptions,
          envoy_dynamic_module_callback_network_get_socket_options(nullptr, nullptr))
WEAK_STUB(ListenerFilterSchedulerCommit,
          envoy_dynamic_module_callback_listener_filter_scheduler_commit(nullptr, 0))
WEAK_STUB(ListenerFilterSchedulerDelete,
          envoy_dynamic_module_callback_listener_filter_scheduler_delete(nullptr))
WEAK_STUB(ListenerFilterConfigSchedulerDelete,
          envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(nullptr))
WEAK_STUB(ListenerFilterConfigSchedulerCommit,
          envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(nullptr, 0))
WEAK_STUB(AccessLoggerGetBytesInfo,
          envoy_dynamic_module_callback_access_logger_get_bytes_info(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetTimingInfo,
          envoy_dynamic_module_callback_access_logger_get_timing_info(nullptr, nullptr))
WEAK_STUB(ListenerFilterCloseSocket,
          envoy_dynamic_module_callback_listener_filter_close_socket(nullptr, {nullptr, 0}))
WEAK_STUB(ListenerFilterSetDownstreamTransportFailureReason,
          envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
              nullptr, {nullptr, 0}))
WEAK_STUB(ListenerFilterSetDynamicMetadataString,
          envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
              nullptr, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}))
WEAK_STUB(ListenerFilterUseOriginalDst,
          envoy_dynamic_module_callback_listener_filter_use_original_dst(nullptr, false))

WEAK_STUB(NetworkFilterGetReadBufferChunks,
          envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(nullptr, nullptr))
WEAK_STUB(NetworkFilterGetWriteBufferChunks,
          envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(nullptr, nullptr))
WEAK_STUB(NetworkFilterDrainReadBuffer,
          envoy_dynamic_module_callback_network_filter_drain_read_buffer(nullptr, 0))
WEAK_STUB(NetworkFilterDrainWriteBuffer,
          envoy_dynamic_module_callback_network_filter_drain_write_buffer(nullptr, 0))
WEAK_STUB(NetworkFilterPrependReadBuffer,
          envoy_dynamic_module_callback_network_filter_prepend_read_buffer(nullptr, {nullptr, 0}))
WEAK_STUB(NetworkFilterAppendReadBuffer,
          envoy_dynamic_module_callback_network_filter_append_read_buffer(nullptr, {nullptr, 0}))
WEAK_STUB(NetworkFilterPrependWriteBuffer,
          envoy_dynamic_module_callback_network_filter_prepend_write_buffer(nullptr, {nullptr, 0}))
WEAK_STUB(NetworkFilterAppendWriteBuffer,
          envoy_dynamic_module_callback_network_filter_append_write_buffer(nullptr, {nullptr, 0}))
WEAK_STUB(NetworkFilterGetRemoteAddress,
          envoy_dynamic_module_callback_network_filter_get_remote_address(nullptr, nullptr,
                                                                          nullptr))
WEAK_STUB(NetworkFilterGetLocalAddress,
          envoy_dynamic_module_callback_network_filter_get_local_address(nullptr, nullptr, nullptr))
WEAK_STUB(NetworkFilterIsSsl, envoy_dynamic_module_callback_network_filter_is_ssl(nullptr))
WEAK_STUB(NetworkFilterGetRequestedServerName,
          envoy_dynamic_module_callback_network_filter_get_requested_server_name(nullptr, nullptr))
WEAK_STUB(NetworkFilterGetDirectRemoteAddress,
          envoy_dynamic_module_callback_network_filter_get_direct_remote_address(nullptr, nullptr,
                                                                                 nullptr))
WEAK_STUB(NetworkFilterGetSslUriSans,
          envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(nullptr, nullptr))
WEAK_STUB(NetworkFilterGetSslDnsSans,
          envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(nullptr, nullptr))
WEAK_STUB(NetworkFilterGetSslSubject,
          envoy_dynamic_module_callback_network_filter_get_ssl_subject(nullptr, nullptr))
WEAK_STUB(NetworkSetFilterStateBytes,
          envoy_dynamic_module_callback_network_set_filter_state_bytes(nullptr, {nullptr, 0},
                                                                       {nullptr, 0}))
WEAK_STUB(NetworkGetFilterStateBytes,
          envoy_dynamic_module_callback_network_get_filter_state_bytes(nullptr, {nullptr, 0},
                                                                       nullptr))
WEAK_STUB(NetworkSetFilterStateTyped,
          envoy_dynamic_module_callback_network_set_filter_state_typed(nullptr, {nullptr, 0},
                                                                       {nullptr, 0}))
WEAK_STUB(NetworkGetFilterStateTyped,
          envoy_dynamic_module_callback_network_get_filter_state_typed(nullptr, {nullptr, 0},
                                                                       nullptr))
WEAK_STUB(NetworkGetDynamicMetadataString,
          envoy_dynamic_module_callback_network_get_dynamic_metadata_string(nullptr, {nullptr, 0},
                                                                            {nullptr, 0}, nullptr))
WEAK_STUB(NetworkGetDynamicMetadataNumber,
          envoy_dynamic_module_callback_network_get_dynamic_metadata_number(nullptr, {nullptr, 0},
                                                                            {nullptr, 0}, nullptr))
WEAK_STUB(NetworkFilterGetClusterHostCount,
          envoy_dynamic_module_callback_network_filter_get_cluster_host_count(nullptr, {nullptr, 0},
                                                                              0, nullptr, nullptr,
                                                                              nullptr))
WEAK_STUB(NetworkFilterGetUpstreamHostAddress,
          envoy_dynamic_module_callback_network_filter_get_upstream_host_address(nullptr, nullptr,
                                                                                 nullptr))
WEAK_STUB(NetworkFilterGetUpstreamHostHostname,
          envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(nullptr, nullptr))
WEAK_STUB(NetworkFilterGetUpstreamHostCluster,
          envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(nullptr, nullptr))
WEAK_STUB(NetworkFilterHasUpstreamHost,
          envoy_dynamic_module_callback_network_filter_has_upstream_host(nullptr))
WEAK_STUB(NetworkFilterStartUpstreamSecureTransport,
          envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(nullptr))
WEAK_STUB(NetworkFilterReadEnabled,
          envoy_dynamic_module_callback_network_filter_read_enabled(nullptr))
WEAK_STUB(NetworkFilterIsHalfCloseEnabled,
          envoy_dynamic_module_callback_network_filter_is_half_close_enabled(nullptr))
WEAK_STUB(NetworkFilterAboveHighWatermark,
          envoy_dynamic_module_callback_network_filter_above_high_watermark(nullptr))
WEAK_STUB(NetworkGetSocketOptionInt,
          envoy_dynamic_module_callback_network_get_socket_option_int(
              nullptr, 0, 0, envoy_dynamic_module_type_socket_option_state_Prebind, nullptr))
WEAK_STUB(NetworkGetSocketOptionBytes,
          envoy_dynamic_module_callback_network_get_socket_option_bytes(
              nullptr, 0, 0, envoy_dynamic_module_type_socket_option_state_Prebind, nullptr))
WEAK_STUB(ListenerFilterGetBufferChunk,
          envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(nullptr, nullptr))
WEAK_STUB(ListenerFilterDrainBuffer,
          envoy_dynamic_module_callback_listener_filter_drain_buffer(nullptr, 0))
WEAK_STUB(ListenerFilterGetRemoteAddress,
          envoy_dynamic_module_callback_listener_filter_get_remote_address(nullptr, nullptr,
                                                                           nullptr))
WEAK_STUB(ListenerFilterGetDirectRemoteAddress,
          envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(nullptr, nullptr,
                                                                                  nullptr))
WEAK_STUB(ListenerFilterGetLocalAddress,
          envoy_dynamic_module_callback_listener_filter_get_local_address(nullptr, nullptr,
                                                                          nullptr))
WEAK_STUB(ListenerFilterGetDirectLocalAddress,
          envoy_dynamic_module_callback_listener_filter_get_direct_local_address(nullptr, nullptr,
                                                                                 nullptr))
WEAK_STUB(AccessLoggerGetConnectionTerminationDetails,
          envoy_dynamic_module_callback_access_logger_get_connection_termination_details(nullptr,
                                                                                         nullptr))
WEAK_STUB(AccessLoggerGetDownstreamDirectLocalAddress,
          envoy_dynamic_module_callback_access_logger_get_downstream_direct_local_address(nullptr,
                                                                                          nullptr,
                                                                                          nullptr))
WEAK_STUB(AccessLoggerGetDownstreamDirectRemoteAddress,
          envoy_dynamic_module_callback_access_logger_get_downstream_direct_remote_address(nullptr,
                                                                                           nullptr,
                                                                                           nullptr))
WEAK_STUB(AccessLoggerGetDownstreamLocalAddress,
          envoy_dynamic_module_callback_access_logger_get_downstream_local_address(nullptr, nullptr,
                                                                                   nullptr))
WEAK_STUB(AccessLoggerGetDownstreamLocalDnsSan,
          envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san(nullptr,
                                                                                   nullptr))
WEAK_STUB(AccessLoggerGetDownstreamLocalSubject,
          envoy_dynamic_module_callback_access_logger_get_downstream_local_subject(nullptr,
                                                                                   nullptr))
WEAK_STUB(AccessLoggerGetDownstreamLocalUriSan,
          envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san(nullptr,
                                                                                   nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerCertDigest,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(nullptr,
                                                                                      nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerCertPresented,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_presented(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerCertValidated,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_validated(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerDnsSan,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerFingerprint1,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_fingerprint_1(nullptr,
                                                                                        nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerIssuer,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_issuer(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerSerial,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_serial(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerSubject,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerUriSan,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDownstreamRemoteAddress,
          envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(nullptr,
                                                                                    nullptr,
                                                                                    nullptr))
WEAK_STUB(AccessLoggerGetDownstreamTlsCipher,
          envoy_dynamic_module_callback_access_logger_get_downstream_tls_cipher(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDownstreamTlsSessionId,
          envoy_dynamic_module_callback_access_logger_get_downstream_tls_session_id(nullptr,
                                                                                    nullptr))
WEAK_STUB(AccessLoggerGetDownstreamTlsVersion,
          envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDownstreamTransportFailureReason,
          envoy_dynamic_module_callback_access_logger_get_downstream_transport_failure_reason(
              nullptr, nullptr))
WEAK_STUB(AccessLoggerGetDynamicMetadata,
          envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(nullptr, {nullptr, 0},
                                                                           {nullptr, 0}, nullptr))
WEAK_STUB(AccessLoggerGetFilterState,
          envoy_dynamic_module_callback_access_logger_get_filter_state(nullptr, {nullptr, 0},
                                                                       nullptr))
WEAK_STUB(AccessLoggerGetHeaderValue,
          envoy_dynamic_module_callback_access_logger_get_header_value(
              nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader, {nullptr, 0},
              nullptr, 0, nullptr))
WEAK_STUB(AccessLoggerGetHeaders,
          envoy_dynamic_module_callback_access_logger_get_headers(
              nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader, nullptr))
WEAK_STUB(AccessLoggerGetJa3Hash,
          envoy_dynamic_module_callback_access_logger_get_ja3_hash(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetJa4Hash,
          envoy_dynamic_module_callback_access_logger_get_ja4_hash(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetLocalReplyBody,
          envoy_dynamic_module_callback_access_logger_get_local_reply_body(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetProtocol,
          envoy_dynamic_module_callback_access_logger_get_protocol(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetRequestId,
          envoy_dynamic_module_callback_access_logger_get_request_id(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetRequestedServerName,
          envoy_dynamic_module_callback_access_logger_get_requested_server_name(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetResponseCodeDetails,
          envoy_dynamic_module_callback_access_logger_get_response_code_details(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetRouteName,
          envoy_dynamic_module_callback_access_logger_get_route_name(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetSpanId,
          envoy_dynamic_module_callback_access_logger_get_span_id(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetTraceId,
          envoy_dynamic_module_callback_access_logger_get_trace_id(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamCluster,
          envoy_dynamic_module_callback_access_logger_get_upstream_cluster(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamHost,
          envoy_dynamic_module_callback_access_logger_get_upstream_host(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamLocalAddress,
          envoy_dynamic_module_callback_access_logger_get_upstream_local_address(nullptr, nullptr,
                                                                                 nullptr))
WEAK_STUB(AccessLoggerGetUpstreamLocalDnsSan,
          envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamLocalSubject,
          envoy_dynamic_module_callback_access_logger_get_upstream_local_subject(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamLocalUriSan,
          envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerCertDigest,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_digest(nullptr,
                                                                                    nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerDnsSan,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerIssuer,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_issuer(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerSubject,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_subject(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerUriSan,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamProtocol,
          envoy_dynamic_module_callback_access_logger_get_upstream_protocol(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamRemoteAddress,
          envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(nullptr, nullptr,
                                                                                  nullptr))
WEAK_STUB(AccessLoggerGetUpstreamTlsCipher,
          envoy_dynamic_module_callback_access_logger_get_upstream_tls_cipher(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamTlsSessionId,
          envoy_dynamic_module_callback_access_logger_get_upstream_tls_session_id(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamTlsVersion,
          envoy_dynamic_module_callback_access_logger_get_upstream_tls_version(nullptr, nullptr))
WEAK_STUB(AccessLoggerGetUpstreamTransportFailureReason,
          envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
              nullptr, nullptr))
WEAK_STUB(AccessLoggerGetVirtualClusterName,
          envoy_dynamic_module_callback_access_logger_get_virtual_cluster_name(nullptr, nullptr))
WEAK_STUB(AccessLoggerHasResponseFlag,
          envoy_dynamic_module_callback_access_logger_has_response_flag(
              nullptr, envoy_dynamic_module_type_response_flag_FailedLocalHealthCheck))
WEAK_STUB(AccessLoggerIsHealthCheck,
          envoy_dynamic_module_callback_access_logger_is_health_check(nullptr))
WEAK_STUB(AccessLoggerIsMtls, envoy_dynamic_module_callback_access_logger_is_mtls(nullptr))
WEAK_STUB(AccessLoggerIsTraceSampled,
          envoy_dynamic_module_callback_access_logger_is_trace_sampled(nullptr))
WEAK_STUB(ListenerFilterGetDetectedTransportProtocol,
          envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(nullptr,
                                                                                        nullptr))
WEAK_STUB(ListenerFilterGetDynamicMetadataString,
          envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
              nullptr, {nullptr, 0}, {nullptr, 0}, nullptr))
WEAK_STUB(ListenerFilterGetJa3Hash,
          envoy_dynamic_module_callback_listener_filter_get_ja3_hash(nullptr, nullptr))
WEAK_STUB(ListenerFilterGetJa4Hash,
          envoy_dynamic_module_callback_listener_filter_get_ja4_hash(nullptr, nullptr))
WEAK_STUB(ListenerFilterGetOriginalDst,
          envoy_dynamic_module_callback_listener_filter_get_original_dst(nullptr, nullptr, nullptr))
WEAK_STUB(ListenerFilterGetRequestedApplicationProtocols,
          envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
              nullptr, nullptr))
WEAK_STUB(ListenerFilterGetRequestedServerName,
          envoy_dynamic_module_callback_listener_filter_get_requested_server_name(nullptr, nullptr))
WEAK_STUB(ListenerFilterGetSocketOptionBytes,
          envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(nullptr, 0, 0,
                                                                                nullptr, 0,
                                                                                nullptr))
WEAK_STUB(ListenerFilterGetSocketOptionInt,
          envoy_dynamic_module_callback_listener_filter_get_socket_option_int(nullptr, 0, 0,
                                                                              nullptr))
WEAK_STUB(ListenerFilterGetSslDnsSans,
          envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(nullptr, nullptr))
WEAK_STUB(ListenerFilterGetSslSubject,
          envoy_dynamic_module_callback_listener_filter_get_ssl_subject(nullptr, nullptr))
WEAK_STUB(ListenerFilterGetSslUriSans,
          envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(nullptr, nullptr))
WEAK_STUB(ListenerFilterIsLocalAddressRestored,
          envoy_dynamic_module_callback_listener_filter_is_local_address_restored(nullptr))
WEAK_STUB(ListenerFilterIsSsl, envoy_dynamic_module_callback_listener_filter_is_ssl(nullptr))
WEAK_STUB(ListenerFilterSetSocketOptionBytes,
          envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(nullptr, 0, 0,
                                                                                {nullptr, 0}))
WEAK_STUB(ListenerFilterSetSocketOptionInt,
          envoy_dynamic_module_callback_listener_filter_set_socket_option_int(nullptr, 0, 0, 0))
WEAK_STUB(UdpListenerFilterGetDatagramDataChunks,
          envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(nullptr,
                                                                                     nullptr))
WEAK_STUB(UdpListenerFilterGetLocalAddress,
          envoy_dynamic_module_callback_udp_listener_filter_get_local_address(nullptr, nullptr,
                                                                              nullptr))
WEAK_STUB(UdpListenerFilterGetPeerAddress,
          envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(nullptr, nullptr,
                                                                             nullptr))
WEAK_STUB(UdpListenerFilterSendDatagram,
          envoy_dynamic_module_callback_udp_listener_filter_send_datagram(nullptr, {nullptr, 0},
                                                                          {nullptr, 0}, 0))
WEAK_STUB(UdpListenerFilterSetDatagramData,
          envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(nullptr,
                                                                              {nullptr, 0}))

WEAK_STUB(NetworkFilterGetReadBufferChunksSize,
          envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(nullptr))
WEAK_STUB(NetworkFilterGetReadBufferSize,
          envoy_dynamic_module_callback_network_filter_get_read_buffer_size(nullptr))
WEAK_STUB(NetworkFilterGetWriteBufferChunksSize,
          envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(nullptr))
WEAK_STUB(NetworkFilterGetWriteBufferSize,
          envoy_dynamic_module_callback_network_filter_get_write_buffer_size(nullptr))
WEAK_STUB(NetworkFilterGetConnectionId,
          envoy_dynamic_module_callback_network_filter_get_connection_id(nullptr))
WEAK_STUB(NetworkFilterGetSslUriSansSize,
          envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(nullptr))
WEAK_STUB(NetworkFilterGetSslDnsSansSize,
          envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(nullptr))
WEAK_STUB(NetworkFilterGetBufferLimit,
          envoy_dynamic_module_callback_network_filter_get_buffer_limit(nullptr))
WEAK_STUB(NetworkFilterGetWorkerIndex,
          envoy_dynamic_module_callback_network_filter_get_worker_index(nullptr))
WEAK_STUB(NetworkGetSocketOptionsSize,
          envoy_dynamic_module_callback_network_get_socket_options_size(nullptr))
WEAK_STUB(AccessLoggerGetAttemptCount,
          envoy_dynamic_module_callback_access_logger_get_attempt_count(nullptr))
WEAK_STUB(AccessLoggerGetConnectionId,
          envoy_dynamic_module_callback_access_logger_get_connection_id(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamLocalDnsSanSize,
          envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san_size(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamLocalUriSanSize,
          envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san_size(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerCertVEnd,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_end(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerCertVStart,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_start(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerDnsSanSize,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san_size(nullptr))
WEAK_STUB(AccessLoggerGetDownstreamPeerUriSanSize,
          envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san_size(nullptr))
WEAK_STUB(AccessLoggerGetHeadersSize,
          envoy_dynamic_module_callback_access_logger_get_headers_size(
              nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader))
WEAK_STUB(AccessLoggerGetRequestHeadersBytes,
          envoy_dynamic_module_callback_access_logger_get_request_headers_bytes(nullptr))
WEAK_STUB(AccessLoggerGetResponseCode,
          envoy_dynamic_module_callback_access_logger_get_response_code(nullptr))
WEAK_STUB(AccessLoggerGetResponseFlags,
          envoy_dynamic_module_callback_access_logger_get_response_flags(nullptr))
WEAK_STUB(AccessLoggerGetResponseHeadersBytes,
          envoy_dynamic_module_callback_access_logger_get_response_headers_bytes(nullptr))
WEAK_STUB(AccessLoggerGetResponseTrailersBytes,
          envoy_dynamic_module_callback_access_logger_get_response_trailers_bytes(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamConnectionId,
          envoy_dynamic_module_callback_access_logger_get_upstream_connection_id(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamLocalDnsSanSize,
          envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san_size(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamLocalUriSanSize,
          envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san_size(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerCertVEnd,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_end(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerCertVStart,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_start(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerDnsSanSize,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san_size(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPeerUriSanSize,
          envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san_size(nullptr))
WEAK_STUB(AccessLoggerGetUpstreamPoolReadyDurationNs,
          envoy_dynamic_module_callback_access_logger_get_upstream_pool_ready_duration_ns(nullptr))
WEAK_STUB(AccessLoggerGetWorkerIndex,
          envoy_dynamic_module_callback_access_logger_get_worker_index(nullptr))
WEAK_STUB(ListenerFilterGetConnectionStartTimeMs,
          envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(nullptr))
WEAK_STUB(
    ListenerFilterGetRequestedApplicationProtocolsSize,
    envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(nullptr))
WEAK_STUB(ListenerFilterGetSocketFd,
          envoy_dynamic_module_callback_listener_filter_get_socket_fd(nullptr))
WEAK_STUB(ListenerFilterGetSslDnsSansSize,
          envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(nullptr))
WEAK_STUB(ListenerFilterGetSslUriSansSize,
          envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(nullptr))
WEAK_STUB(ListenerFilterGetWorkerIndex,
          envoy_dynamic_module_callback_listener_filter_get_worker_index(nullptr))
WEAK_STUB(ListenerFilterMaxReadBytes,
          envoy_dynamic_module_callback_listener_filter_max_read_bytes(nullptr))
WEAK_STUB(ListenerFilterWriteToSocket,
          envoy_dynamic_module_callback_listener_filter_write_to_socket(nullptr, {nullptr, 0}))
WEAK_STUB(UdpListenerFilterGetDatagramDataChunksSize,
          envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(nullptr))
WEAK_STUB(UdpListenerFilterGetDatagramDataSize,
          envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(nullptr))
WEAK_STUB(UdpListenerFilterGetWorkerIndex,
          envoy_dynamic_module_callback_udp_listener_filter_get_worker_index(nullptr))

WEAK_STUB(NetworkFilterSchedulerNew,
          envoy_dynamic_module_callback_network_filter_scheduler_new(nullptr))
WEAK_STUB(NetworkFilterConfigSchedulerNew,
          envoy_dynamic_module_callback_network_filter_config_scheduler_new(nullptr))
WEAK_STUB(ListenerFilterSchedulerNew,
          envoy_dynamic_module_callback_listener_filter_scheduler_new(nullptr))
WEAK_STUB(ListenerFilterConfigSchedulerNew,
          envoy_dynamic_module_callback_listener_filter_config_scheduler_new(nullptr))

WEAK_STUB(NetworkFilterConfigDefineCounter,
          envoy_dynamic_module_callback_network_filter_config_define_counter(nullptr, {nullptr, 0},
                                                                             nullptr))
WEAK_STUB(NetworkFilterIncrementCounter,
          envoy_dynamic_module_callback_network_filter_increment_counter(nullptr, 0, 0))
WEAK_STUB(NetworkFilterConfigDefineGauge,
          envoy_dynamic_module_callback_network_filter_config_define_gauge(nullptr, {nullptr, 0},
                                                                           nullptr))
WEAK_STUB(NetworkFilterSetGauge,
          envoy_dynamic_module_callback_network_filter_set_gauge(nullptr, 0, 0))
WEAK_STUB(NetworkFilterIncrementGauge,
          envoy_dynamic_module_callback_network_filter_increment_gauge(nullptr, 0, 0))
WEAK_STUB(NetworkFilterDecrementGauge,
          envoy_dynamic_module_callback_network_filter_decrement_gauge(nullptr, 0, 0))
WEAK_STUB(NetworkFilterConfigDefineHistogram,
          envoy_dynamic_module_callback_network_filter_config_define_histogram(nullptr,
                                                                               {nullptr, 0},
                                                                               nullptr))
WEAK_STUB(NetworkFilterRecordHistogramValue,
          envoy_dynamic_module_callback_network_filter_record_histogram_value(nullptr, 0, 0))
WEAK_STUB(ListenerFilterConfigDefineCounter,
          envoy_dynamic_module_callback_listener_filter_config_define_counter(nullptr, {nullptr, 0},
                                                                              nullptr))
WEAK_STUB(ListenerFilterConfigDefineGauge,
          envoy_dynamic_module_callback_listener_filter_config_define_gauge(nullptr, {nullptr, 0},
                                                                            nullptr))
WEAK_STUB(ListenerFilterConfigDefineHistogram,
          envoy_dynamic_module_callback_listener_filter_config_define_histogram(nullptr,
                                                                                {nullptr, 0},
                                                                                nullptr))
WEAK_STUB(AccessLoggerConfigDefineCounter,
          envoy_dynamic_module_callback_access_logger_config_define_counter(nullptr, {nullptr, 0},
                                                                            nullptr))
WEAK_STUB(AccessLoggerConfigDefineGauge,
          envoy_dynamic_module_callback_access_logger_config_define_gauge(nullptr, {nullptr, 0},
                                                                          nullptr))
WEAK_STUB(AccessLoggerConfigDefineHistogram,
          envoy_dynamic_module_callback_access_logger_config_define_histogram(nullptr, {nullptr, 0},
                                                                              nullptr))
WEAK_STUB(AccessLoggerDecrementGauge,
          envoy_dynamic_module_callback_access_logger_decrement_gauge(nullptr, 0, 0))
WEAK_STUB(AccessLoggerIncrementCounter,
          envoy_dynamic_module_callback_access_logger_increment_counter(nullptr, 0, 0))
WEAK_STUB(AccessLoggerIncrementGauge,
          envoy_dynamic_module_callback_access_logger_increment_gauge(nullptr, 0, 0))
WEAK_STUB(AccessLoggerRecordHistogramValue,
          envoy_dynamic_module_callback_access_logger_record_histogram_value(nullptr, 0, 0))
WEAK_STUB(AccessLoggerSetGauge,
          envoy_dynamic_module_callback_access_logger_set_gauge(nullptr, 0, 0))
WEAK_STUB(ListenerFilterDecrementGauge,
          envoy_dynamic_module_callback_listener_filter_decrement_gauge(nullptr, 0, 0))
WEAK_STUB(ListenerFilterIncrementCounter,
          envoy_dynamic_module_callback_listener_filter_increment_counter(nullptr, 0, 0))
WEAK_STUB(ListenerFilterIncrementGauge,
          envoy_dynamic_module_callback_listener_filter_increment_gauge(nullptr, 0, 0))
WEAK_STUB(ListenerFilterRecordHistogramValue,
          envoy_dynamic_module_callback_listener_filter_record_histogram_value(nullptr, 0, 0))
WEAK_STUB(ListenerFilterSetGauge,
          envoy_dynamic_module_callback_listener_filter_set_gauge(nullptr, 0, 0))
WEAK_STUB(UdpListenerFilterConfigDefineCounter,
          envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(nullptr,
                                                                                  {nullptr, 0},
                                                                                  nullptr))
WEAK_STUB(UdpListenerFilterConfigDefineGauge,
          envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(nullptr,
                                                                                {nullptr, 0},
                                                                                nullptr))
WEAK_STUB(UdpListenerFilterConfigDefineHistogram,
          envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(nullptr,
                                                                                    {nullptr, 0},
                                                                                    nullptr))
WEAK_STUB(UdpListenerFilterDecrementGauge,
          envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(nullptr, 0, 0))
WEAK_STUB(UdpListenerFilterIncrementCounter,
          envoy_dynamic_module_callback_udp_listener_filter_increment_counter(nullptr, 0, 0))
WEAK_STUB(UdpListenerFilterIncrementGauge,
          envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(nullptr, 0, 0))
WEAK_STUB(UdpListenerFilterRecordHistogramValue,
          envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(nullptr, 0, 0))
WEAK_STUB(UdpListenerFilterSetGauge,
          envoy_dynamic_module_callback_udp_listener_filter_set_gauge(nullptr, 0, 0))

WEAK_STUB(NetworkFilterHttpCallout,
          envoy_dynamic_module_callback_network_filter_http_callout(nullptr, nullptr, {nullptr, 0},
                                                                    nullptr, 0, {nullptr, 0}, 0))
WEAK_STUB(ListenerFilterHttpCallout,
          envoy_dynamic_module_callback_listener_filter_http_callout(nullptr, nullptr, {nullptr, 0},
                                                                     nullptr, 0, {nullptr, 0}, 0))

WEAK_STUB(NetworkFilterGetConnectionState,
          envoy_dynamic_module_callback_network_filter_get_connection_state(nullptr))
WEAK_STUB(NetworkFilterReadDisable,
          envoy_dynamic_module_callback_network_filter_read_disable(nullptr, true))

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
