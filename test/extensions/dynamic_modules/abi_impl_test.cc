#include "source/extensions/dynamic_modules/abi/abi.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

// =============================================================================
// Validation Mode Tests
// =============================================================================

TEST(CommonAbiImplTest, IsValidationModeReturnsTrueInValidateMode) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ON_CALL(context.options_, mode()).WillByDefault(Return(Server::Mode::Validate));

  ScopedThreadLocalServerContextSetter setter(context);
  EXPECT_TRUE(envoy_dynamic_module_callback_is_validation_mode());
}

TEST(CommonAbiImplTest, IsValidationModeReturnsFalseInServeMode) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ON_CALL(context.options_, mode()).WillByDefault(Return(Server::Mode::Serve));

  ScopedThreadLocalServerContextSetter setter(context);
  EXPECT_FALSE(envoy_dynamic_module_callback_is_validation_mode());
}

TEST(CommonAbiImplTest, IsValidationModeReturnsFalseInInitOnlyMode) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ON_CALL(context.options_, mode()).WillByDefault(Return(Server::Mode::InitOnly));

  ScopedThreadLocalServerContextSetter setter(context);
  EXPECT_FALSE(envoy_dynamic_module_callback_is_validation_mode());
}

// =============================================================================
// Function Registry Tests
// =============================================================================

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

// =============================================================================
// Shared Data Registry Tests
// =============================================================================

// Test registering and retrieving shared data.
TEST(CommonAbiImplTest, SharedDataRegistryRegisterAndGet) {
  int data = 42;
  envoy_dynamic_module_type_module_buffer key = {"sd_basic", 8};

  EXPECT_TRUE(
      envoy_dynamic_module_callback_register_shared_data(key, reinterpret_cast<void*>(&data)));

  void* out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_data(key, &out));
  EXPECT_NE(out, nullptr);
  EXPECT_EQ(*reinterpret_cast<int*>(out), 42);
}

// Test that getting a non-existent key returns false.
TEST(CommonAbiImplTest, SharedDataRegistryGetNonExistent) {
  envoy_dynamic_module_type_module_buffer key = {"sd_nonexistent", 14};
  void* out = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_get_shared_data(key, &out));
  EXPECT_EQ(out, nullptr);
}

// Test that registering nullptr returns false.
TEST(CommonAbiImplTest, SharedDataRegistryRegisterNull) {
  envoy_dynamic_module_type_module_buffer key = {"sd_null", 7};
  EXPECT_FALSE(envoy_dynamic_module_callback_register_shared_data(key, nullptr));

  // Key should not exist in the registry.
  void* out = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_get_shared_data(key, &out));
}

// Test that overwriting an existing key succeeds and updates the pointer.
TEST(CommonAbiImplTest, SharedDataRegistryOverwrite) {
  int data1 = 100;
  int data2 = 200;
  envoy_dynamic_module_type_module_buffer key = {"sd_overwrite", 12};

  EXPECT_TRUE(
      envoy_dynamic_module_callback_register_shared_data(key, reinterpret_cast<void*>(&data1)));

  // Overwrite with a new pointer.
  EXPECT_TRUE(
      envoy_dynamic_module_callback_register_shared_data(key, reinterpret_cast<void*>(&data2)));

  // The new pointer should be returned.
  void* out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_data(key, &out));
  EXPECT_EQ(*reinterpret_cast<int*>(out), 200);
}

// Test multiple independent keys.
TEST(CommonAbiImplTest, SharedDataRegistryMultipleKeys) {
  int data_a = 10;
  int data_b = 20;
  envoy_dynamic_module_type_module_buffer key_a = {"sd_multi_a", 10};
  envoy_dynamic_module_type_module_buffer key_b = {"sd_multi_b", 10};

  EXPECT_TRUE(
      envoy_dynamic_module_callback_register_shared_data(key_a, reinterpret_cast<void*>(&data_a)));
  EXPECT_TRUE(
      envoy_dynamic_module_callback_register_shared_data(key_b, reinterpret_cast<void*>(&data_b)));

  void* out_a = nullptr;
  void* out_b = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_data(key_a, &out_a));
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_data(key_b, &out_b));

  EXPECT_EQ(*reinterpret_cast<int*>(out_a), 10);
  EXPECT_EQ(*reinterpret_cast<int*>(out_b), 20);
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

WEAK_STUB(BootstrapExtensionConfigSchedulerNew,
          envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(nullptr))
WEAK_STUB(BootstrapExtensionConfigSchedulerDelete,
          envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(nullptr))
WEAK_STUB(BootstrapExtensionConfigSchedulerCommit,
          envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(nullptr, 0))
WEAK_STUB(BootstrapExtensionConfigSignalInitComplete,
          envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(nullptr))

WEAK_STUB(BootstrapExtensionHttpCallout,
          envoy_dynamic_module_callback_bootstrap_extension_http_callout(nullptr, nullptr,
                                                                         {"cluster", 7}, nullptr, 0,
                                                                         {nullptr, 0}, 5000))

WEAK_STUB(BootstrapExtensionGetCounterValue,
          envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(nullptr, {nullptr, 0},
                                                                              nullptr))
WEAK_STUB(BootstrapExtensionGetGaugeValue,
          envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(nullptr, {nullptr, 0},
                                                                            nullptr))
WEAK_STUB(BootstrapExtensionGetHistogramSummary,
          envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(nullptr,
                                                                                  {nullptr, 0},
                                                                                  nullptr, nullptr))

WEAK_STUB(BootstrapExtensionIterateCounters,
          envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(nullptr, nullptr,
                                                                             nullptr))
WEAK_STUB(BootstrapExtensionIterateGauges,
          envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(nullptr, nullptr,
                                                                           nullptr))

WEAK_STUB(BootstrapExtensionTimerNew,
          envoy_dynamic_module_callback_bootstrap_extension_timer_new(nullptr))

WEAK_STUB(BootstrapExtensionTimerEnable,
          envoy_dynamic_module_callback_bootstrap_extension_timer_enable(nullptr, 100))
WEAK_STUB(BootstrapExtensionTimerDisable,
          envoy_dynamic_module_callback_bootstrap_extension_timer_disable(nullptr))

WEAK_STUB(BootstrapExtensionTimerEnabled,
          envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(nullptr))

WEAK_STUB(BootstrapExtensionTimerDelete,
          envoy_dynamic_module_callback_bootstrap_extension_timer_delete(nullptr))

WEAK_STUB(BootstrapExtensionRegisterAdminHandler,
          envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
              nullptr, {"/test", 5}, {"help", 4}, true, false))
WEAK_STUB(BootstrapExtensionRemoveAdminHandler,
          envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(nullptr,
                                                                                 {"/test", 5}))

WEAK_STUB(BootstrapExtensionAdminSetResponse,
          envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(nullptr,
                                                                               {nullptr, 0}))

WEAK_STUB(BootstrapExtensionConfigDefineCounter,
          envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
              nullptr, {"counter", 7}, nullptr, 0, nullptr))
WEAK_STUB(BootstrapExtensionConfigIncrementCounter,
          envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(nullptr, 0,
                                                                                     nullptr, 0, 1))

WEAK_STUB(BootstrapExtensionConfigDefineGauge,
          envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
              nullptr, {"gauge", 5}, nullptr, 0, nullptr))

WEAK_STUB(BootstrapExtensionConfigSetGauge,
          envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(nullptr, 0, nullptr, 0,
                                                                             42))

WEAK_STUB(BootstrapExtensionConfigIncrementGauge,
          envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(nullptr, 0,
                                                                                   nullptr, 0, 1))

WEAK_STUB(BootstrapExtensionConfigDecrementGauge,
          envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(nullptr, 0,
                                                                                   nullptr, 0, 1))

WEAK_STUB(BootstrapExtensionConfigDefineHistogram,
          envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
              nullptr, {"histogram", 9}, nullptr, 0, nullptr))

WEAK_STUB(BootstrapExtensionConfigRecordHistogramValue,
          envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
              nullptr, 0, nullptr, 0, 100))

WEAK_STUB(CertValidatorSetErrorDetails,
          envoy_dynamic_module_callback_cert_validator_set_error_details(nullptr, {nullptr, 0}))
WEAK_STUB(CertValidatorSetFilterState,
          envoy_dynamic_module_callback_cert_validator_set_filter_state(nullptr, {nullptr, 0},
                                                                        {nullptr, 0}))
WEAK_STUB(CertValidatorGetFilterState,
          envoy_dynamic_module_callback_cert_validator_get_filter_state(nullptr, {nullptr, 0},
                                                                        nullptr))

WEAK_STUB(ClusterAddHosts,
          envoy_dynamic_module_callback_cluster_add_hosts(nullptr, 0, nullptr, nullptr, nullptr,
                                                          nullptr, nullptr, nullptr, 0, 0, nullptr))
WEAK_STUB(ClusterRemoveHosts,
          envoy_dynamic_module_callback_cluster_remove_hosts(nullptr, nullptr, 0))
WEAK_STUB(ClusterPreInitComplete, envoy_dynamic_module_callback_cluster_pre_init_complete(nullptr))
WEAK_STUB(ClusterLbGetHealthyHostCount,
          envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(nullptr, 0))
WEAK_STUB(ClusterLbGetHealthyHost,
          envoy_dynamic_module_callback_cluster_lb_get_healthy_host(nullptr, 0, 0))
WEAK_STUB(ClusterLbContextComputeHashKey,
          envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(nullptr, nullptr))
WEAK_STUB(ClusterLbContextGetDownstreamHeadersSize,
          envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(nullptr))
WEAK_STUB(ClusterLbContextGetDownstreamHeaders,
          envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(nullptr, nullptr))
WEAK_STUB(ClusterLbContextGetDownstreamHeader,
          envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
              nullptr, {nullptr, 0}, nullptr, 0, nullptr))
WEAK_STUB(ClusterLbContextGetHostSelectionRetryCount,
          envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(nullptr))
WEAK_STUB(ClusterLbContextShouldSelectAnotherHost,
          envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(nullptr,
                                                                                      nullptr, 0,
                                                                                      0))
WEAK_STUB(ClusterLbContextGetOverrideHost,
          envoy_dynamic_module_callback_cluster_lb_context_get_override_host(nullptr, nullptr,
                                                                             nullptr))
WEAK_STUB(ClusterLbContextGetDownstreamConnectionSni,
          envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(nullptr,
                                                                                         nullptr))
WEAK_STUB(ClusterLbGetClusterName,
          envoy_dynamic_module_callback_cluster_lb_get_cluster_name(nullptr, nullptr))
WEAK_STUB(ClusterLbGetHostsCount,
          envoy_dynamic_module_callback_cluster_lb_get_hosts_count(nullptr, 0))
WEAK_STUB(ClusterLbGetDegradedHostsCount,
          envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(nullptr, 0))
WEAK_STUB(ClusterLbGetPrioritySetSize,
          envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(nullptr))
WEAK_STUB(ClusterLbGetHealthyHostAddress,
          envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(nullptr, 0, 0, nullptr))
WEAK_STUB(ClusterLbGetHealthyHostWeight,
          envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(nullptr, 0, 0))
WEAK_STUB(ClusterLbGetHostHealth,
          envoy_dynamic_module_callback_cluster_lb_get_host_health(nullptr, 0, 0))
WEAK_STUB(ClusterLbGetHostHealthByAddress,
          envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(nullptr, {nullptr, 0},
                                                                              nullptr))
WEAK_STUB(ClusterLbGetHostAddress,
          envoy_dynamic_module_callback_cluster_lb_get_host_address(nullptr, 0, 0, nullptr))
WEAK_STUB(ClusterLbGetHostWeight,
          envoy_dynamic_module_callback_cluster_lb_get_host_weight(nullptr, 0, 0))
WEAK_STUB(ClusterLbGetHostStat, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                                    nullptr, 0, 0, envoy_dynamic_module_type_host_stat_RqTotal))
WEAK_STUB(ClusterLbGetHostLocality,
          envoy_dynamic_module_callback_cluster_lb_get_host_locality(nullptr, 0, 0, nullptr,
                                                                     nullptr, nullptr))
WEAK_STUB(ClusterLbSetHostData,
          envoy_dynamic_module_callback_cluster_lb_set_host_data(nullptr, 0, 0, 0))
WEAK_STUB(ClusterLbGetHostData,
          envoy_dynamic_module_callback_cluster_lb_get_host_data(nullptr, 0, 0, nullptr))
WEAK_STUB(ClusterLbGetHostMetadataString,
          envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(nullptr, 0, 0,
                                                                            {nullptr, 0},
                                                                            {nullptr, 0}, nullptr))
WEAK_STUB(ClusterLbGetHostMetadataNumber,
          envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(nullptr, 0, 0,
                                                                            {nullptr, 0},
                                                                            {nullptr, 0}, nullptr))
WEAK_STUB(ClusterLbGetHostMetadataBool,
          envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(nullptr, 0, 0,
                                                                          {nullptr, 0},
                                                                          {nullptr, 0}, nullptr))
WEAK_STUB(ClusterLbGetLocalityCount,
          envoy_dynamic_module_callback_cluster_lb_get_locality_count(nullptr, 0))
WEAK_STUB(ClusterLbGetLocalityHostCount,
          envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(nullptr, 0, 0))
WEAK_STUB(ClusterLbGetLocalityHostAddress,
          envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(nullptr, 0, 0, 0,
                                                                             nullptr))
WEAK_STUB(ClusterLbGetLocalityWeight,
          envoy_dynamic_module_callback_cluster_lb_get_locality_weight(nullptr, 0, 0))
WEAK_STUB(ClusterLbAsyncHostSelectionComplete,
          envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(nullptr, nullptr,
                                                                                 nullptr,
                                                                                 {nullptr, 0}))
WEAK_STUB(ClusterLbGetMemberUpdateHostAddress,
          envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(nullptr, 0, true,
                                                                                  nullptr))
WEAK_STUB(ClusterUpdateHostHealth,
          envoy_dynamic_module_callback_cluster_update_host_health(
              nullptr, nullptr, envoy_dynamic_module_type_host_health_Healthy))
WEAK_STUB(ClusterFindHostByAddress,
          envoy_dynamic_module_callback_cluster_find_host_by_address(nullptr, {nullptr, 0}))
WEAK_STUB(ClusterLbFindHostByAddress,
          envoy_dynamic_module_callback_cluster_lb_find_host_by_address(nullptr, {nullptr, 0}))
WEAK_STUB(ClusterLbGetHost, envoy_dynamic_module_callback_cluster_lb_get_host(nullptr, 0, 0))
WEAK_STUB(ClusterSchedulerNew, envoy_dynamic_module_callback_cluster_scheduler_new(nullptr))
WEAK_STUB(ClusterSchedulerDelete, envoy_dynamic_module_callback_cluster_scheduler_delete(nullptr))
WEAK_STUB(ClusterSchedulerCommit,
          envoy_dynamic_module_callback_cluster_scheduler_commit(nullptr, 0))
WEAK_STUB(ClusterConfigDefineCounter,
          envoy_dynamic_module_callback_cluster_config_define_counter(nullptr, {nullptr, 0},
                                                                      nullptr, 0, nullptr))
WEAK_STUB(ClusterConfigIncrementCounter,
          envoy_dynamic_module_callback_cluster_config_increment_counter(nullptr, 0, nullptr, 0, 0))
WEAK_STUB(ClusterConfigDefineGauge,
          envoy_dynamic_module_callback_cluster_config_define_gauge(nullptr, {nullptr, 0}, nullptr,
                                                                    0, nullptr))
WEAK_STUB(ClusterConfigSetGauge,
          envoy_dynamic_module_callback_cluster_config_set_gauge(nullptr, 0, nullptr, 0, 0))
WEAK_STUB(ClusterConfigIncrementGauge,
          envoy_dynamic_module_callback_cluster_config_increment_gauge(nullptr, 0, nullptr, 0, 0))
WEAK_STUB(ClusterConfigDecrementGauge,
          envoy_dynamic_module_callback_cluster_config_decrement_gauge(nullptr, 0, nullptr, 0, 0))
WEAK_STUB(ClusterConfigDefineHistogram,
          envoy_dynamic_module_callback_cluster_config_define_histogram(nullptr, {nullptr, 0},
                                                                        nullptr, 0, nullptr))
WEAK_STUB(ClusterConfigRecordHistogramValue,
          envoy_dynamic_module_callback_cluster_config_record_histogram_value(nullptr, 0, nullptr,
                                                                              0, 0))

WEAK_STUB(LbGetClusterName, envoy_dynamic_module_callback_lb_get_cluster_name(nullptr, nullptr))
WEAK_STUB(LbGetHostsCount, envoy_dynamic_module_callback_lb_get_hosts_count(nullptr, 0))
WEAK_STUB(LbGetHealthyHostsCount,
          envoy_dynamic_module_callback_lb_get_healthy_hosts_count(nullptr, 0))
WEAK_STUB(LbGetDegradedHostsCount,
          envoy_dynamic_module_callback_lb_get_degraded_hosts_count(nullptr, 0))
WEAK_STUB(LbGetPrioritySetSize, envoy_dynamic_module_callback_lb_get_priority_set_size(nullptr))
WEAK_STUB(LbGetHealthyHostAddress,
          envoy_dynamic_module_callback_lb_get_healthy_host_address(nullptr, 0, 0, nullptr))
WEAK_STUB(LbGetHealthyHostWeight,
          envoy_dynamic_module_callback_lb_get_healthy_host_weight(nullptr, 0, 0))
WEAK_STUB(LbGetHostHealth, envoy_dynamic_module_callback_lb_get_host_health(nullptr, 0, 0))
WEAK_STUB(LbGetHostHealthByAddress,
          envoy_dynamic_module_callback_lb_get_host_health_by_address(nullptr, {nullptr, 0},
                                                                      nullptr))
WEAK_STUB(LbGetHostAddress,
          envoy_dynamic_module_callback_lb_get_host_address(nullptr, 0, 0, nullptr))
WEAK_STUB(LbGetHostWeight, envoy_dynamic_module_callback_lb_get_host_weight(nullptr, 0, 0))
WEAK_STUB(LbGetHostLocality,
          envoy_dynamic_module_callback_lb_get_host_locality(nullptr, 0, 0, nullptr, nullptr,
                                                             nullptr))
WEAK_STUB(LbContextComputeHashKey,
          envoy_dynamic_module_callback_lb_context_compute_hash_key(nullptr, nullptr))
WEAK_STUB(LbContextGetDownstreamHeadersSize,
          envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(nullptr))
WEAK_STUB(LbContextGetDownstreamHeaders,
          envoy_dynamic_module_callback_lb_context_get_downstream_headers(nullptr, nullptr))
WEAK_STUB(LbContextGetDownstreamHeader,
          envoy_dynamic_module_callback_lb_context_get_downstream_header(nullptr, {nullptr, 0},
                                                                         nullptr, 0, nullptr))
WEAK_STUB(LbContextGetHostSelectionRetryCount,
          envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(nullptr))
WEAK_STUB(LbContextShouldSelectAnotherHost,
          envoy_dynamic_module_callback_lb_context_should_select_another_host(nullptr, nullptr, 0,
                                                                              0))
WEAK_STUB(LbContextGetOverrideHost,
          envoy_dynamic_module_callback_lb_context_get_override_host(nullptr, nullptr, nullptr))
WEAK_STUB(LbSetHostData, envoy_dynamic_module_callback_lb_set_host_data(nullptr, 0, 0, 42))
WEAK_STUB(LbGetHostData, envoy_dynamic_module_callback_lb_get_host_data(nullptr, 0, 0, nullptr))
WEAK_STUB(LbGetHostMetadataString,
          envoy_dynamic_module_callback_lb_get_host_metadata_string(nullptr, 0, 0, {nullptr, 0},
                                                                    {nullptr, 0}, nullptr))
WEAK_STUB(LbGetHostMetadataNumber,
          envoy_dynamic_module_callback_lb_get_host_metadata_number(nullptr, 0, 0, {nullptr, 0},
                                                                    {nullptr, 0}, nullptr))
WEAK_STUB(LbGetHostMetadataBool,
          envoy_dynamic_module_callback_lb_get_host_metadata_bool(nullptr, 0, 0, {nullptr, 0},
                                                                  {nullptr, 0}, nullptr))
WEAK_STUB(LbGetLocalityCount, envoy_dynamic_module_callback_lb_get_locality_count(nullptr, 0))
WEAK_STUB(LbGetLocalityHostCount,
          envoy_dynamic_module_callback_lb_get_locality_host_count(nullptr, 0, 0))
WEAK_STUB(LbGetLocalityHostAddress,
          envoy_dynamic_module_callback_lb_get_locality_host_address(nullptr, 0, 0, 0, nullptr))
WEAK_STUB(LbGetLocalityWeight, envoy_dynamic_module_callback_lb_get_locality_weight(nullptr, 0, 0))
WEAK_STUB(LbGetMemberUpdateHostAddress,
          envoy_dynamic_module_callback_lb_get_member_update_host_address(nullptr, 0, true,
                                                                          nullptr))
WEAK_STUB(LbGetHostStat, envoy_dynamic_module_callback_lb_get_host_stat(
                             nullptr, 0, 0, envoy_dynamic_module_type_host_stat_RqTotal))

WEAK_STUB(LbConfigDefineCounter,
          envoy_dynamic_module_callback_lb_config_define_counter(nullptr, {"counter", 7}, nullptr,
                                                                 0, nullptr))
WEAK_STUB(LbConfigIncrementCounter,
          envoy_dynamic_module_callback_lb_config_increment_counter(nullptr, 0, nullptr, 0, 1))
WEAK_STUB(LbConfigDefineGauge,
          envoy_dynamic_module_callback_lb_config_define_gauge(nullptr, {"gauge", 5}, nullptr, 0,
                                                               nullptr))
WEAK_STUB(LbConfigSetGauge,
          envoy_dynamic_module_callback_lb_config_set_gauge(nullptr, 0, nullptr, 0, 42))
WEAK_STUB(LbConfigIncrementGauge,
          envoy_dynamic_module_callback_lb_config_increment_gauge(nullptr, 0, nullptr, 0, 1))
WEAK_STUB(LbConfigDecrementGauge,
          envoy_dynamic_module_callback_lb_config_decrement_gauge(nullptr, 0, nullptr, 0, 1))
WEAK_STUB(LbConfigDefineHistogram,
          envoy_dynamic_module_callback_lb_config_define_histogram(nullptr, {"histogram", 9},
                                                                   nullptr, 0, nullptr))
WEAK_STUB(LbConfigRecordHistogramValue,
          envoy_dynamic_module_callback_lb_config_record_histogram_value(nullptr, 0, nullptr, 0,
                                                                         100))

WEAK_STUB(MatcherGetHeadersSize,
          envoy_dynamic_module_callback_matcher_get_headers_size(
              nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader))
WEAK_STUB(MatcherGetHeaders,
          envoy_dynamic_module_callback_matcher_get_headers(
              nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader, nullptr))
WEAK_STUB(MatcherGetHeaderValue,
          envoy_dynamic_module_callback_matcher_get_header_value(
              nullptr, envoy_dynamic_module_type_http_header_type_RequestHeader, {nullptr, 0},
              nullptr, 0, nullptr))

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
WEAK_STUB(AccessLoggerGetAttributeBool,
          envoy_dynamic_module_callback_access_logger_get_attribute_bool(
              nullptr, envoy_dynamic_module_type_attribute_id_ConnectionMtls, nullptr))
WEAK_STUB(AccessLoggerGetAttributeInt,
          envoy_dynamic_module_callback_access_logger_get_attribute_int(
              nullptr, envoy_dynamic_module_type_attribute_id_ResponseCode, nullptr))
WEAK_STUB(AccessLoggerGetAttributeString,
          envoy_dynamic_module_callback_access_logger_get_attribute_string(
              nullptr, envoy_dynamic_module_type_attribute_id_RequestProtocol, nullptr))
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

WEAK_STUB(UpstreamBridgeGetRequestHeader,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
              nullptr, {nullptr, 0}, nullptr, 0, nullptr))
WEAK_STUB(UpstreamBridgeGetRequestHeadersSize,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(nullptr))
WEAK_STUB(UpstreamBridgeGetRequestHeaders,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(nullptr,
                                                                                     nullptr))
WEAK_STUB(UpstreamBridgeGetRequestBuffer,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(nullptr,
                                                                                    nullptr,
                                                                                    nullptr))
WEAK_STUB(UpstreamBridgeGetResponseBuffer,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(nullptr,
                                                                                     nullptr,
                                                                                     nullptr))
WEAK_STUB(UpstreamBridgeSendUpstreamData,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(nullptr,
                                                                                    {nullptr, 0},
                                                                                    false))
WEAK_STUB(UpstreamBridgeSendResponse,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(nullptr, 0, nullptr,
                                                                               0, {nullptr, 0}))
WEAK_STUB(UpstreamBridgeSendResponseHeaders,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(nullptr, 0,
                                                                                       nullptr, 0,
                                                                                       false))
WEAK_STUB(UpstreamBridgeSendResponseData,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(nullptr,
                                                                                    {nullptr, 0},
                                                                                    false))
WEAK_STUB(UpstreamBridgeSendResponseTrailers,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(nullptr,
                                                                                        nullptr, 0))

WEAK_STUB(NetworkSetDynamicMetadataBool,
          envoy_dynamic_module_callback_network_set_dynamic_metadata_bool(nullptr, {nullptr, 0},
                                                                          {nullptr, 0}, true))
WEAK_STUB(NetworkGetDynamicMetadataBool,
          envoy_dynamic_module_callback_network_get_dynamic_metadata_bool(nullptr, {nullptr, 0},
                                                                          {nullptr, 0}, nullptr))

WEAK_STUB(ListenerFilterGetAddressType,
          envoy_dynamic_module_callback_listener_filter_get_address_type(nullptr))

WEAK_STUB(ListenerFilterGetDynamicMetadataNumber,
          envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number(
              nullptr, {nullptr, 0}, {nullptr, 0}, nullptr))
WEAK_STUB(ListenerFilterSetDynamicMetadataNumber,
          envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number(
              nullptr, {nullptr, 0}, {nullptr, 0}, 0))

WEAK_STUB(HttpAddDynamicMetadataListNumber,
          envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number(nullptr, {nullptr, 0},
                                                                              {nullptr, 0}, 0))
WEAK_STUB(HttpAddDynamicMetadataListString,
          envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string(nullptr, {nullptr, 0},
                                                                              {nullptr, 0},
                                                                              {nullptr, 0}))
WEAK_STUB(HttpAddDynamicMetadataListBool,
          envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool(nullptr, {nullptr, 0},
                                                                            {nullptr, 0}, true))
WEAK_STUB(HttpGetMetadataListSize, envoy_dynamic_module_callback_http_get_metadata_list_size(
                                       nullptr, envoy_dynamic_module_type_metadata_source_Dynamic,
                                       {nullptr, 0}, {nullptr, 0}, nullptr))
WEAK_STUB(HttpGetMetadataListNumber, envoy_dynamic_module_callback_http_get_metadata_list_number(
                                         nullptr, envoy_dynamic_module_type_metadata_source_Dynamic,
                                         {nullptr, 0}, {nullptr, 0}, 0, nullptr))
WEAK_STUB(HttpGetMetadataListString, envoy_dynamic_module_callback_http_get_metadata_list_string(
                                         nullptr, envoy_dynamic_module_type_metadata_source_Dynamic,
                                         {nullptr, 0}, {nullptr, 0}, 0, nullptr))
WEAK_STUB(HttpGetMetadataListBool, envoy_dynamic_module_callback_http_get_metadata_list_bool(
                                       nullptr, envoy_dynamic_module_type_metadata_source_Dynamic,
                                       {nullptr, 0}, {nullptr, 0}, 0, nullptr))

WEAK_STUB(TracerGetTraceContextValue,
          envoy_dynamic_module_callback_tracer_get_trace_context_value(nullptr, {nullptr, 0},
                                                                       nullptr))
WEAK_STUB(TracerSetTraceContextValue,
          envoy_dynamic_module_callback_tracer_set_trace_context_value(nullptr, {nullptr, 0},
                                                                       {nullptr, 0}))
WEAK_STUB(TracerRemoveTraceContextValue,
          envoy_dynamic_module_callback_tracer_remove_trace_context_value(nullptr, {nullptr, 0}))
WEAK_STUB(TracerGetTraceContextProtocol,
          envoy_dynamic_module_callback_tracer_get_trace_context_protocol(nullptr, nullptr))
WEAK_STUB(TracerGetTraceContextHost,
          envoy_dynamic_module_callback_tracer_get_trace_context_host(nullptr, nullptr))
WEAK_STUB(TracerGetTraceContextPath,
          envoy_dynamic_module_callback_tracer_get_trace_context_path(nullptr, nullptr))
WEAK_STUB(TracerGetTraceContextMethod,
          envoy_dynamic_module_callback_tracer_get_trace_context_method(nullptr, nullptr))
WEAK_STUB(TracerDefineCounter,
          envoy_dynamic_module_callback_tracer_define_counter(nullptr, {nullptr, 0}, nullptr, 0,
                                                              nullptr))
WEAK_STUB(TracerDefineGauge,
          envoy_dynamic_module_callback_tracer_define_gauge(nullptr, {nullptr, 0}, nullptr, 0,
                                                            nullptr))
WEAK_STUB(TracerDefineHistogram,
          envoy_dynamic_module_callback_tracer_define_histogram(nullptr, {nullptr, 0}, nullptr, 0,
                                                                nullptr))
WEAK_STUB(TracerIncrementCounter,
          envoy_dynamic_module_callback_tracer_increment_counter(nullptr, 0, nullptr, 0, 0))
WEAK_STUB(TracerRecordHistogramValue,
          envoy_dynamic_module_callback_tracer_record_histogram_value(nullptr, 0, nullptr, 0, 0))
WEAK_STUB(TracerSetGauge, envoy_dynamic_module_callback_tracer_set_gauge(nullptr, 0, nullptr, 0, 0))

WEAK_STUB(DnsResolveComplete,
          envoy_dynamic_module_callback_dns_resolve_complete(
              nullptr, 0, envoy_dynamic_module_type_dns_resolution_status_Completed, {nullptr, 0},
              nullptr, 0))
WEAK_STUB(DnsResolverConfigDefineCounter,
          envoy_dynamic_module_callback_dns_resolver_config_define_counter(nullptr, {nullptr, 0},
                                                                           nullptr, 0, nullptr))
WEAK_STUB(DnsResolverConfigIncrementCounter,
          envoy_dynamic_module_callback_dns_resolver_config_increment_counter(nullptr, 0, nullptr,
                                                                              0, 0))
WEAK_STUB(DnsResolverConfigDefineGauge,
          envoy_dynamic_module_callback_dns_resolver_config_define_gauge(nullptr, {nullptr, 0},
                                                                         nullptr, 0, nullptr))
WEAK_STUB(DnsResolverConfigSetGauge,
          envoy_dynamic_module_callback_dns_resolver_config_set_gauge(nullptr, 0, nullptr, 0, 0))
WEAK_STUB(DnsResolverConfigIncrementGauge,
          envoy_dynamic_module_callback_dns_resolver_config_increment_gauge(nullptr, 0, nullptr, 0,
                                                                            0))
WEAK_STUB(DnsResolverConfigDecrementGauge,
          envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge(nullptr, 0, nullptr, 0,
                                                                            0))
WEAK_STUB(DnsResolverConfigDefineHistogram,
          envoy_dynamic_module_callback_dns_resolver_config_define_histogram(nullptr, {nullptr, 0},
                                                                             nullptr, 0, nullptr))
WEAK_STUB(DnsResolverConfigRecordHistogramValue,
          envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value(nullptr, 0,
                                                                                   nullptr, 0, 0))

WEAK_STUB(TransportSocketGetIoHandle,
          envoy_dynamic_module_callback_transport_socket_get_io_handle(nullptr))
WEAK_STUB(TransportSocketIoHandleRead,
          envoy_dynamic_module_callback_transport_socket_io_handle_read(nullptr, nullptr, 0,
                                                                        nullptr))
WEAK_STUB(TransportSocketIoHandleWrite,
          envoy_dynamic_module_callback_transport_socket_io_handle_write(nullptr, nullptr, 0,
                                                                         nullptr))
WEAK_STUB(TransportSocketIoHandleFd,
          envoy_dynamic_module_callback_transport_socket_io_handle_fd(nullptr))
WEAK_STUB(TransportSocketReadBufferDrain,
          envoy_dynamic_module_callback_transport_socket_read_buffer_drain(nullptr, 0))
WEAK_STUB(TransportSocketReadBufferAdd,
          envoy_dynamic_module_callback_transport_socket_read_buffer_add(nullptr, nullptr, 0))
WEAK_STUB(TransportSocketReadBufferLength,
          envoy_dynamic_module_callback_transport_socket_read_buffer_length(nullptr))
WEAK_STUB(TransportSocketWriteBufferDrain,
          envoy_dynamic_module_callback_transport_socket_write_buffer_drain(nullptr, 0))
WEAK_STUB(TransportSocketWriteBufferGetSlices,
          envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(nullptr, nullptr,
                                                                                 nullptr))
WEAK_STUB(TransportSocketWriteBufferLength,
          envoy_dynamic_module_callback_transport_socket_write_buffer_length(nullptr))
WEAK_STUB(TransportSocketRaiseEvent,
          envoy_dynamic_module_callback_transport_socket_raise_event(
              nullptr, envoy_dynamic_module_type_network_connection_event_Connected))
WEAK_STUB(TransportSocketShouldDrainReadBuffer,
          envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(nullptr))
WEAK_STUB(TransportSocketSetIsReadable,
          envoy_dynamic_module_callback_transport_socket_set_is_readable(nullptr))
WEAK_STUB(TransportSocketFlushWriteBuffer,
          envoy_dynamic_module_callback_transport_socket_flush_write_buffer(nullptr))

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
