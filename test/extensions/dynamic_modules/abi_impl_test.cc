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

// ---------------------- Shared state registry tests --------------------------------

// Test publishing and retrieving a shared state entry.
TEST(CommonAbiImplTest, SharedStatePublishAndGet) {
  int data = 42;
  envoy_dynamic_module_type_module_buffer key = {"test_key", 8};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data));

  const void* data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_state(key, &data_out));
  EXPECT_EQ(data_out, &data);
  EXPECT_EQ(*static_cast<const int*>(data_out), 42);

  // Clean up.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
}

// Test that getting a non-existent key returns false.
TEST(CommonAbiImplTest, SharedStateGetNonExistent) {
  envoy_dynamic_module_type_module_buffer key = {"non_existent_key", 16};
  const void* data_out = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_get_shared_state(key, &data_out));
  EXPECT_EQ(data_out, nullptr);
}

// Test that publishing nullptr clears the entry (get returns false).
TEST(CommonAbiImplTest, SharedStatePublishNullClearsEntry) {
  int data = 100;
  envoy_dynamic_module_type_module_buffer key = {"clear_key", 9};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data));
  const void* data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_state(key, &data_out));
  EXPECT_EQ(data_out, &data);

  // Clear the entry.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
  data_out = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_get_shared_state(key, &data_out));
  EXPECT_EQ(data_out, nullptr);
}

// Test overwriting an existing shared state entry.
TEST(CommonAbiImplTest, SharedStateOverwrite) {
  int data1 = 1;
  int data2 = 2;
  envoy_dynamic_module_type_module_buffer key = {"overwrite_key", 13};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data1));
  const void* data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_state(key, &data_out));
  EXPECT_EQ(*static_cast<const int*>(data_out), 1);

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data2));
  data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_state(key, &data_out));
  EXPECT_EQ(*static_cast<const int*>(data_out), 2);

  // Clean up.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
}

// Test multiple independent keys.
TEST(CommonAbiImplTest, SharedStateMultipleKeys) {
  int data_a = 10;
  int data_b = 20;
  envoy_dynamic_module_type_module_buffer key_a = {"mkey_a", 6};
  envoy_dynamic_module_type_module_buffer key_b = {"mkey_b", 6};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_a, &data_a));
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_b, &data_b));

  const void* out_a = nullptr;
  const void* out_b = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_state(key_a, &out_a));
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_state(key_b, &out_b));
  EXPECT_EQ(*static_cast<const int*>(out_a), 10);
  EXPECT_EQ(*static_cast<const int*>(out_b), 20);

  // Clearing one key does not affect the other.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_a, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_get_shared_state(key_a, &out_a));
  EXPECT_TRUE(envoy_dynamic_module_callback_get_shared_state(key_b, &out_b));
  EXPECT_EQ(*static_cast<const int*>(out_b), 20);

  // Clean up.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_b, nullptr));
}

// Test clearing a non-existent key (should succeed silently).
TEST(CommonAbiImplTest, SharedStateClearNonExistent) {
  envoy_dynamic_module_type_module_buffer key = {"never_published", 15};
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
}

// ---------------------- Shared state handle tests ----------------------------------

// Test basic handle lifecycle: acquire, read, release.
TEST(CommonAbiImplTest, SharedStateHandleBasic) {
  int data = 42;
  envoy_dynamic_module_type_module_buffer key = {"handle_basic", 12};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data));

  auto handle = envoy_dynamic_module_callback_shared_state_handle_new(key);
  ASSERT_NE(handle, nullptr);

  const void* data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_shared_state_handle_get(handle, &data_out));
  EXPECT_EQ(*static_cast<const int*>(data_out), 42);

  envoy_dynamic_module_callback_shared_state_handle_delete(handle);

  // Clean up.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
}

// Test that handle_new returns nullptr for a non-existent key.
TEST(CommonAbiImplTest, SharedStateHandleNonExistentKey) {
  envoy_dynamic_module_type_module_buffer key = {"no_such_handle", 14};
  auto handle = envoy_dynamic_module_callback_shared_state_handle_new(key);
  EXPECT_EQ(handle, nullptr);
}

// Test that a handle sees atomic updates from the publisher.
TEST(CommonAbiImplTest, SharedStateHandleSeesUpdates) {
  int data1 = 1;
  int data2 = 2;
  envoy_dynamic_module_type_module_buffer key = {"handle_update", 13};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data1));

  auto handle = envoy_dynamic_module_callback_shared_state_handle_new(key);
  ASSERT_NE(handle, nullptr);

  // Initial read.
  const void* data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_shared_state_handle_get(handle, &data_out));
  EXPECT_EQ(*static_cast<const int*>(data_out), 1);

  // Publisher atomically updates the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data2));

  // Handle sees the update.
  data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_shared_state_handle_get(handle, &data_out));
  EXPECT_EQ(*static_cast<const int*>(data_out), 2);

  envoy_dynamic_module_callback_shared_state_handle_delete(handle);
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
}

// Test that a handle returns false after the entry is cleared.
TEST(CommonAbiImplTest, SharedStateHandleAfterClear) {
  int data = 99;
  envoy_dynamic_module_type_module_buffer key = {"handle_clear", 12};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data));

  auto handle = envoy_dynamic_module_callback_shared_state_handle_new(key);
  ASSERT_NE(handle, nullptr);

  // Clear the entry.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));

  // Handle now returns false.
  const void* data_out = nullptr;
  EXPECT_FALSE(envoy_dynamic_module_callback_shared_state_handle_get(handle, &data_out));
  EXPECT_EQ(data_out, nullptr);

  envoy_dynamic_module_callback_shared_state_handle_delete(handle);
}

// Test that a handle sees data after a clear-then-republish cycle.
TEST(CommonAbiImplTest, SharedStateHandleSurvivesClearAndRepublish) {
  int data1 = 10;
  int data2 = 20;
  envoy_dynamic_module_type_module_buffer key = {"handle_republish", 16};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data1));

  auto handle = envoy_dynamic_module_callback_shared_state_handle_new(key);
  ASSERT_NE(handle, nullptr);

  const void* data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_shared_state_handle_get(handle, &data_out));
  EXPECT_EQ(*static_cast<const int*>(data_out), 10);

  // Clear the entry.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_shared_state_handle_get(handle, &data_out));

  // Republish new data under the same key.
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, &data2));

  // Handle sees the new data because the underlying entry was reused.
  data_out = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_shared_state_handle_get(handle, &data_out));
  EXPECT_EQ(*static_cast<const int*>(data_out), 20);

  envoy_dynamic_module_callback_shared_state_handle_delete(handle);
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key, nullptr));
}

// Test multiple handles to different keys.
TEST(CommonAbiImplTest, SharedStateHandleMultipleKeys) {
  int data_x = 100;
  int data_y = 200;
  envoy_dynamic_module_type_module_buffer key_x = {"hkey_x", 6};
  envoy_dynamic_module_type_module_buffer key_y = {"hkey_y", 6};

  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_x, &data_x));
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_y, &data_y));

  auto handle_x = envoy_dynamic_module_callback_shared_state_handle_new(key_x);
  auto handle_y = envoy_dynamic_module_callback_shared_state_handle_new(key_y);
  ASSERT_NE(handle_x, nullptr);
  ASSERT_NE(handle_y, nullptr);

  const void* out_x = nullptr;
  const void* out_y = nullptr;
  EXPECT_TRUE(envoy_dynamic_module_callback_shared_state_handle_get(handle_x, &out_x));
  EXPECT_TRUE(envoy_dynamic_module_callback_shared_state_handle_get(handle_y, &out_y));
  EXPECT_EQ(*static_cast<const int*>(out_x), 100);
  EXPECT_EQ(*static_cast<const int*>(out_y), 200);

  envoy_dynamic_module_callback_shared_state_handle_delete(handle_x);
  envoy_dynamic_module_callback_shared_state_handle_delete(handle_y);
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_x, nullptr));
  EXPECT_TRUE(envoy_dynamic_module_callback_publish_shared_state(key_y, nullptr));
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
