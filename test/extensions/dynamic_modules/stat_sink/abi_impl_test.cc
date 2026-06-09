#include <string>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

#include "test/extensions/dynamic_modules/stat_sink/test_util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

// Exercises the C ABI snapshot callbacks the module uses to read counters, gauges, and text
// readouts through the per-flush snapshot handle. Names and values are decoded straight into
// module-provided buffers, so the tests pass their own buffers and read back the reported size.
class DynamicModuleStatsSinkAbiTest : public testing::Test {
public:
  void SetUp() override {
    ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));
  }

  // Returns the bytes actually written into a module buffer, clamped to its capacity so a
  // truncated result is observable.
  static std::string written(const char* buffer, size_t size, size_t capacity) {
    return std::string(buffer, std::min(size, capacity));
  }

  // The snapshot callbacks take the opaque per-flush handle, so wrap the mock snapshot in a flush
  // context and pass its address.
  envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshotHandle() { return &context_; }

  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  DynamicModuleStatsSinkFlushContext context_{snapshot_};

  NiceMock<Stats::MockCounter> c0_, c1_, c2_;
  NiceMock<Stats::MockGauge> g0_, g1_;
  ConcreteMockTextReadout t0_, t1_;
};

// =============================================================================
// Counter callbacks
// =============================================================================

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterCountEmpty) {
  EXPECT_EQ(0u,
            envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(snapshotHandle()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterCount) {
  c0_.name_ = "requests_total";
  c0_.value_ = 10;
  c1_.name_ = "errors_total";
  c1_.value_ = 2;
  snapshot_.counters_.push_back({/*delta=*/5, c0_});
  snapshot_.counters_.push_back({/*delta=*/1, c1_});

  EXPECT_EQ(2u,
            envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(snapshotHandle()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterValid) {
  c0_.name_ = "requests_total";
  c0_.value_ = 100;
  snapshot_.counters_.push_back({/*delta=*/7, c0_});

  char name_buffer[256];
  size_t name_size = 0;
  uint64_t value_out = 0;
  uint64_t delta_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 0, name_buffer, sizeof(name_buffer), &name_size, &value_out, &delta_out));
  EXPECT_EQ("requests_total", written(name_buffer, name_size, sizeof(name_buffer)));
  EXPECT_EQ(100u, value_out);
  EXPECT_EQ(7u, delta_out);
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterIndexMany) {
  c0_.name_ = "a";
  c0_.value_ = 1;
  c1_.name_ = "b";
  c1_.value_ = 2;
  c2_.name_ = "c";
  c2_.value_ = 3;
  snapshot_.counters_.push_back({10, c0_});
  snapshot_.counters_.push_back({20, c1_});
  snapshot_.counters_.push_back({30, c2_});

  struct Expected {
    const char* name;
    uint64_t value;
    uint64_t delta;
  } expected[] = {{"a", 1, 10}, {"b", 2, 20}, {"c", 3, 30}};

  for (size_t i = 0; i < 3; i++) {
    char name_buffer[256];
    size_t name_size = 0;
    uint64_t value_out = 0;
    uint64_t delta_out = 0;
    ASSERT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
        snapshotHandle(), i, name_buffer, sizeof(name_buffer), &name_size, &value_out, &delta_out));
    EXPECT_EQ(expected[i].name, written(name_buffer, name_size, sizeof(name_buffer)));
    EXPECT_EQ(expected[i].value, value_out);
    EXPECT_EQ(expected[i].delta, delta_out);
  }
}

// A buffer smaller than the name receives a truncated prefix, but name_size reports the full
// length so the module can retry with a large enough buffer.
TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterNameTruncatedThenRetry) {
  c0_.name_ = "requests_total"; // 14 bytes.
  c0_.value_ = 1;
  snapshot_.counters_.push_back({/*delta=*/2, c0_});

  char small[5];
  size_t name_size = 0;
  uint64_t value_out = 0;
  uint64_t delta_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 0, small, sizeof(small), &name_size, &value_out, &delta_out));
  EXPECT_EQ(14u, name_size);
  EXPECT_EQ("reque", written(small, name_size, sizeof(small)));
  EXPECT_EQ(1u, value_out);
  EXPECT_EQ(2u, delta_out);

  std::string retry(name_size, '\0');
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 0, retry.data(), retry.size(), &name_size, &value_out, &delta_out));
  EXPECT_EQ(14u, name_size);
  EXPECT_EQ("requests_total", written(retry.data(), name_size, retry.size()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterOutOfRange) {
  c0_.name_ = "only";
  c0_.value_ = 1;
  snapshot_.counters_.push_back({1, c0_});

  // The ABI contract is that no outputs are written when the index is out of range, so seed the
  // outputs (including the first buffer byte) with sentinels and verify they survive the false
  // return.
  char name_buffer[256] = {'Z'};
  size_t name_size = 12345;
  uint64_t value_out = 999;
  uint64_t delta_out = 888;
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 1, name_buffer, sizeof(name_buffer), &name_size, &value_out, &delta_out));
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 42, name_buffer, sizeof(name_buffer), &name_size, &value_out, &delta_out));
  EXPECT_EQ(12345u, name_size);
  EXPECT_EQ(999u, value_out);
  EXPECT_EQ(888u, delta_out);
  EXPECT_EQ('Z', name_buffer[0]);
}

// =============================================================================
// Gauge callbacks
// =============================================================================

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeCountEmpty) {
  EXPECT_EQ(0u, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(snapshotHandle()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeCount) {
  g0_.name_ = "in_flight";
  g0_.value_ = 3;
  g1_.name_ = "memory_bytes";
  g1_.value_ = 12345;
  snapshot_.gauges_.push_back(g0_);
  snapshot_.gauges_.push_back(g1_);

  EXPECT_EQ(2u, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(snapshotHandle()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeValid) {
  g0_.name_ = "in_flight";
  g0_.value_ = 7;
  snapshot_.gauges_.push_back(g0_);

  char name_buffer[256];
  size_t name_size = 0;
  uint64_t value_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
      snapshotHandle(), 0, name_buffer, sizeof(name_buffer), &name_size, &value_out));
  EXPECT_EQ("in_flight", written(name_buffer, name_size, sizeof(name_buffer)));
  EXPECT_EQ(7u, value_out);
}

// A buffer smaller than the gauge name receives a truncated prefix, but name_size reports the full
// length so the module can retry with a large enough buffer.
TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeNameTruncatedThenRetry) {
  g0_.name_ = "memory_allocated"; // 16 bytes.
  g0_.value_ = 42;
  snapshot_.gauges_.push_back(g0_);

  char small[5];
  size_t name_size = 0;
  uint64_t value_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
      snapshotHandle(), 0, small, sizeof(small), &name_size, &value_out));
  EXPECT_EQ(16u, name_size);
  EXPECT_EQ("memor", written(small, name_size, sizeof(small)));
  EXPECT_EQ(42u, value_out);

  std::string retry(name_size, '\0');
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
      snapshotHandle(), 0, retry.data(), retry.size(), &name_size, &value_out));
  EXPECT_EQ(16u, name_size);
  EXPECT_EQ("memory_allocated", written(retry.data(), name_size, retry.size()));
  EXPECT_EQ(42u, value_out);
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeOutOfRange) {
  g0_.name_ = "only";
  g0_.value_ = 1;
  snapshot_.gauges_.push_back(g0_);

  // No outputs may be written on a false return, so seed the size and the first buffer byte with
  // sentinels and verify they survive.
  char name_buffer[256] = {'Z'};
  size_t name_size = 12345;
  uint64_t value_out = 999;
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
      snapshotHandle(), 1, name_buffer, sizeof(name_buffer), &name_size, &value_out));
  EXPECT_EQ(12345u, name_size);
  EXPECT_EQ(999u, value_out);
  EXPECT_EQ('Z', name_buffer[0]);
}

// =============================================================================
// Text readout callbacks
// =============================================================================

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutCountEmpty) {
  EXPECT_EQ(0u, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(
                    snapshotHandle()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutCount) {
  t0_.name_ = "version";
  t0_.value_ = "1.0.0";
  t1_.name_ = "control_plane";
  t1_.value_ = "pilot-1";
  snapshot_.text_readouts_.push_back(t0_);
  snapshot_.text_readouts_.push_back(t1_);

  EXPECT_EQ(2u, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(
                    snapshotHandle()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutValid) {
  t0_.name_ = "version";
  t0_.value_ = "1.28.0";
  snapshot_.text_readouts_.push_back(t0_);

  char name_buffer[256];
  size_t name_size = 0;
  char value_buffer[256];
  size_t value_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 0, name_buffer, sizeof(name_buffer), &name_size, value_buffer,
      sizeof(value_buffer), &value_size));
  EXPECT_EQ("version", written(name_buffer, name_size, sizeof(name_buffer)));
  EXPECT_EQ("1.28.0", written(value_buffer, value_size, sizeof(value_buffer)));
}

// An empty value reports size 0 and writes nothing, while the name still decodes normally.
TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutEmptyValue) {
  t0_.name_ = "version";
  t0_.value_ = "";
  snapshot_.text_readouts_.push_back(t0_);

  char name_buffer[256];
  size_t name_size = 0;
  char value_buffer[256] = {'Y'};
  size_t value_size = 999;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 0, name_buffer, sizeof(name_buffer), &name_size, value_buffer,
      sizeof(value_buffer), &value_size));
  EXPECT_EQ("version", written(name_buffer, name_size, sizeof(name_buffer)));
  EXPECT_EQ(0u, value_size);
  EXPECT_EQ('Y', value_buffer[0]);
}

// A zero-capacity value buffer is a length query, so the full value size is reported and nothing is
// written even when the value is non-empty. This is the first call a module makes before sizing its
// buffer, so the null buffer must not be de-referenced.
TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutValueLengthQuery) {
  t0_.name_ = "version";
  t0_.value_ = "1.28.0"; // 6 bytes.
  snapshot_.text_readouts_.push_back(t0_);

  char name_buffer[256];
  size_t name_size = 0;
  size_t value_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 0, name_buffer, sizeof(name_buffer), &name_size, nullptr, 0, &value_size));
  EXPECT_EQ("version", written(name_buffer, name_size, sizeof(name_buffer)));
  EXPECT_EQ(6u, value_size);
}

// Both the name and value honor the truncation-and-retry contract independently.
TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutValueTruncated) {
  t0_.name_ = "control_plane";
  t0_.value_ = "pilot-1.28.0"; // 12 bytes.
  snapshot_.text_readouts_.push_back(t0_);

  char name_buffer[256];
  size_t name_size = 0;
  char small_value[4];
  size_t value_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 0, name_buffer, sizeof(name_buffer), &name_size, small_value,
      sizeof(small_value), &value_size));
  EXPECT_EQ("control_plane", written(name_buffer, name_size, sizeof(name_buffer)));
  EXPECT_EQ(12u, value_size);
  EXPECT_EQ("pilo", written(small_value, value_size, sizeof(small_value)));
}

// The name and value can both be truncated in one call, and name_size and value_size report the
// full lengths so the module can grow both buffers and retry to recover the complete strings.
TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutNameAndValueTruncatedThenRetry) {
  t0_.name_ = "control_plane"; // 13 bytes.
  t0_.value_ = "pilot-1.28.0"; // 12 bytes.
  snapshot_.text_readouts_.push_back(t0_);

  char small_name[5];
  size_t name_size = 0;
  char small_value[4];
  size_t value_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 0, small_name, sizeof(small_name), &name_size, small_value,
      sizeof(small_value), &value_size));
  EXPECT_EQ(13u, name_size);
  EXPECT_EQ("contr", written(small_name, name_size, sizeof(small_name)));
  EXPECT_EQ(12u, value_size);
  EXPECT_EQ("pilo", written(small_value, value_size, sizeof(small_value)));

  std::string name_retry(name_size, '\0');
  std::string value_retry(value_size, '\0');
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 0, name_retry.data(), name_retry.size(), &name_size, value_retry.data(),
      value_retry.size(), &value_size));
  EXPECT_EQ("control_plane", written(name_retry.data(), name_size, name_retry.size()));
  EXPECT_EQ("pilot-1.28.0", written(value_retry.data(), value_size, value_retry.size()));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutOutOfRange) {
  t0_.name_ = "only";
  t0_.value_ = "x";
  snapshot_.text_readouts_.push_back(t0_);

  // No outputs may be written on a false return, so seed both sizes and the first buffer bytes
  // with sentinels and verify they survive.
  char name_buffer[256] = {'Z'};
  size_t name_size = 12345;
  char value_buffer[256] = {'Y'};
  size_t value_size = 54321;
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 1, name_buffer, sizeof(name_buffer), &name_size, value_buffer,
      sizeof(value_buffer), &value_size));
  EXPECT_EQ(12345u, name_size);
  EXPECT_EQ(54321u, value_size);
  EXPECT_EQ('Z', name_buffer[0]);
  EXPECT_EQ('Y', value_buffer[0]);
}

// =============================================================================
// Config gauge and scheduler callbacks
// =============================================================================

// Exercises the C ABI callbacks the module uses to define and publish gauges and to schedule events
// back to the main thread, driving them through a real configuration object.
class DynamicModuleStatsSinkConfigAbiTest : public testing::Test {
public:
  envoy_dynamic_module_type_stat_sink_config_envoy_ptr configHandle() { return config_.get(); }

  static envoy_dynamic_module_type_module_buffer buffer(absl::string_view value) {
    return {value.data(), value.size()};
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  DynamicModuleStatsSinkConfigSharedPtr config_{std::make_shared<DynamicModuleStatsSinkConfig>(
      "test_sink", "test_config", /*dynamic_module=*/nullptr, context_)};
};

TEST_F(DynamicModuleStatsSinkConfigAbiTest, DefineAndSetGauge) {
  size_t id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_stat_sink_config_define_gauge(configHandle(), buffer("g"),
                                                                        &id));
  EXPECT_EQ(1u, id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_stat_sink_config_set_gauge(configHandle(), id, 99));

  auto gauge = TestUtility::findGauge(context_.store_, "g");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(99u, gauge->value());
}

TEST_F(DynamicModuleStatsSinkConfigAbiTest, SetGaugeInvalidIdReturnsMetricNotFound) {
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_stat_sink_config_set_gauge(configHandle(), 1, 1));
}

// The scheduler created, committed, and deleted through the ABI runs the scheduled hook on the main
// thread, which publishes the committed event id into the gauge via the set_gauge ABI callback.
TEST_F(DynamicModuleStatsSinkConfigAbiTest, SchedulerNewCommitDelete) {
  size_t id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_stat_sink_config_define_gauge(configHandle(), buffer("g"),
                                                                        &id));
  config_->on_config_scheduled_ =
      [](envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr,
         envoy_dynamic_module_type_stat_sink_config_module_ptr, uint64_t event_id) {
        envoy_dynamic_module_callback_stat_sink_config_set_gauge(config_envoy_ptr, 1, event_id);
      };

  auto* scheduler = envoy_dynamic_module_callback_stat_sink_config_scheduler_new(configHandle());
  ASSERT_NE(nullptr, scheduler);
  EXPECT_CALL(context_.dispatcher_, post(_)).WillOnce(Invoke([](Event::PostCb cb) { cb(); }));
  envoy_dynamic_module_callback_stat_sink_config_scheduler_commit(scheduler, 77);
  envoy_dynamic_module_callback_stat_sink_config_scheduler_delete(scheduler);

  auto gauge = TestUtility::findGauge(context_.store_, "g");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(77u, gauge->value());
}

} // namespace
} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
