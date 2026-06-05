#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"

#include "test/extensions/dynamic_modules/stat_sink/test_util.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

// Exercises the C ABI snapshot callbacks the module uses to read counters, gauges, and text
// readouts through the per-flush snapshot handle.
class DynamicModuleStatsSinkAbiTest : public testing::Test {
public:
  void SetUp() override {
    ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));
  }

  static std::string toString(const envoy_dynamic_module_type_envoy_buffer& buf) {
    return std::string(buf.ptr, buf.length);
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

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  uint64_t delta_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 0, &name_out, &value_out, &delta_out));
  EXPECT_EQ("requests_total", toString(name_out));
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
    envoy_dynamic_module_type_envoy_buffer name_out{};
    uint64_t value_out = 0;
    uint64_t delta_out = 0;
    ASSERT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
        snapshotHandle(), i, &name_out, &value_out, &delta_out));
    EXPECT_EQ(expected[i].name, toString(name_out));
    EXPECT_EQ(expected[i].value, value_out);
    EXPECT_EQ(expected[i].delta, delta_out);
  }
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterOutOfRange) {
  c0_.name_ = "only";
  c0_.value_ = 1;
  snapshot_.counters_.push_back({1, c0_});

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  uint64_t delta_out = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 1, &name_out, &value_out, &delta_out));
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      snapshotHandle(), 42, &name_out, &value_out, &delta_out));
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

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(snapshotHandle(), 0,
                                                                         &name_out, &value_out));
  EXPECT_EQ("in_flight", toString(name_out));
  EXPECT_EQ(7u, value_out);
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeOutOfRange) {
  g0_.name_ = "only";
  g0_.value_ = 1;
  snapshot_.gauges_.push_back(g0_);

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(snapshotHandle(), 1,
                                                                          &name_out, &value_out));
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

  envoy_dynamic_module_type_envoy_buffer name_out{};
  envoy_dynamic_module_type_envoy_buffer value_out{};
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 0, &name_out, &value_out));
  EXPECT_EQ("version", toString(name_out));
  EXPECT_EQ("1.28.0", toString(value_out));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutOutOfRange) {
  t0_.name_ = "only";
  t0_.value_ = "x";
  snapshot_.text_readouts_.push_back(t0_);

  envoy_dynamic_module_type_envoy_buffer name_out{};
  envoy_dynamic_module_type_envoy_buffer value_out{};
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
      snapshotHandle(), 1, &name_out, &value_out));
}

} // namespace
} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
