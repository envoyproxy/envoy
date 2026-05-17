#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink.h"

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

// MockTextReadout in test/mocks/stats/mocks.h inherits MockMetric<TextReadout>
// directly (not MockStatWithRefcount), so it leaves the refcount methods and
// markUnused unimplemented -- making it abstract. Provide a concrete subclass
// that stubs them out for test use.
class ConcreteMockTextReadout : public NiceMock<Stats::MockTextReadout> {
public:
  // RefcountInterface
  void incRefCount() override { ++ref_count_; }
  bool decRefCount() override { return --ref_count_ == 0; }
  uint32_t use_count() const override { return ref_count_; }
  // Metric
  void markUnused() override { used_ = false; }

private:
  uint32_t ref_count_{1};
};

// Builds a StatSinkFlushContext in the same way DynamicModuleStatsSink::flush
// does, then exercises the C ABI snapshot callbacks the module uses to read
// counters/gauges/text readouts.
class DynamicModuleStatsSinkAbiTest : public testing::Test {
public:
  void SetUp() override {
    // MockMetricSnapshot's default ctor wires every accessor except
    // textReadouts(); set it up once for the whole fixture.
    ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));
  }

  // Reproduces the caching done by DynamicModuleStatsSink::flush() so tests
  // can construct a context without a real sink.
  StatSinkFlushContext buildContext(Stats::MetricSnapshot& snapshot) {
    StatSinkFlushContext ctx;
    ctx.snapshot = &snapshot;

    const auto& counters = snapshot.counters();
    ctx.counter_names.reserve(counters.size());
    for (const auto& c : counters) {
      ctx.counter_names.push_back(c.counter_.get().name());
    }

    const auto& gauges = snapshot.gauges();
    ctx.gauge_names.reserve(gauges.size());
    for (const auto& g : gauges) {
      ctx.gauge_names.push_back(g.get().name());
    }

    const auto& text_readouts = snapshot.textReadouts();
    ctx.text_readout_names.reserve(text_readouts.size());
    ctx.text_readout_values.reserve(text_readouts.size());
    for (const auto& t : text_readouts) {
      ctx.text_readout_names.push_back(t.get().name());
      ctx.text_readout_values.push_back(t.get().value());
    }
    return ctx;
  }

  static std::string toString(const envoy_dynamic_module_type_envoy_buffer& buf) {
    return std::string(buf.ptr, buf.length);
  }

  NiceMock<Stats::MockMetricSnapshot> snapshot_;

  // Reusable metric objects. Populate `name_` / `value_` / `latch_` on the ones
  // you add to the snapshot.
  NiceMock<Stats::MockCounter> c0_, c1_, c2_;
  NiceMock<Stats::MockGauge> g0_, g1_;
  ConcreteMockTextReadout t0_, t1_;
};

// =============================================================================
// Counter callbacks
// =============================================================================

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterCountEmpty) {
  auto ctx = buildContext(snapshot_);
  EXPECT_EQ(0u, envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(&ctx));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterCount) {
  c0_.name_ = "requests_total";
  c0_.value_ = 10;
  c1_.name_ = "errors_total";
  c1_.value_ = 2;
  snapshot_.counters_.push_back({/*delta=*/5, c0_});
  snapshot_.counters_.push_back({/*delta=*/1, c1_});

  auto ctx = buildContext(snapshot_);
  EXPECT_EQ(2u, envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(&ctx));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterValid) {
  c0_.name_ = "requests_total";
  c0_.value_ = 100;
  snapshot_.counters_.push_back({/*delta=*/7, c0_});
  auto ctx = buildContext(snapshot_);

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  uint64_t delta_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(&ctx, 0, &name_out,
                                                                           &value_out, &delta_out));
  EXPECT_EQ("requests_total", toString(name_out));
  EXPECT_EQ(100u, value_out);
  EXPECT_EQ(7u, delta_out);
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterIndexMany) {
  // Three counters, access all of them, verify name/value/delta are correct.
  c0_.name_ = "a";
  c0_.value_ = 1;
  c1_.name_ = "b";
  c1_.value_ = 2;
  c2_.name_ = "c";
  c2_.value_ = 3;
  snapshot_.counters_.push_back({10, c0_});
  snapshot_.counters_.push_back({20, c1_});
  snapshot_.counters_.push_back({30, c2_});
  auto ctx = buildContext(snapshot_);

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
        &ctx, i, &name_out, &value_out, &delta_out));
    EXPECT_EQ(expected[i].name, toString(name_out));
    EXPECT_EQ(expected[i].value, value_out);
    EXPECT_EQ(expected[i].delta, delta_out);
  }
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterOutOfRange) {
  c0_.name_ = "only";
  c0_.value_ = 1;
  snapshot_.counters_.push_back({1, c0_});
  auto ctx = buildContext(snapshot_);

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  uint64_t delta_out = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      &ctx, 1, &name_out, &value_out, &delta_out));
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
      &ctx, 42, &name_out, &value_out, &delta_out));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetCounterUsesCachedName) {
  // Regression: Metric::name() returns std::string by value. If the ABI
  // returned a pointer into a temporary, the returned buffer would dangle.
  // We verify the returned pointer aliases the context's cached name vector,
  // not the mock metric's name_ field.
  c0_.name_ = "cached";
  c0_.value_ = 1;
  snapshot_.counters_.push_back({1, c0_});
  auto ctx = buildContext(snapshot_);

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t v, d;
  ASSERT_TRUE(
      envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(&ctx, 0, &name_out, &v, &d));
  EXPECT_EQ(name_out.ptr, ctx.counter_names[0].data());
}

// =============================================================================
// Gauge callbacks
// =============================================================================

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeCountEmpty) {
  auto ctx = buildContext(snapshot_);
  EXPECT_EQ(0u, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(&ctx));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeCount) {
  g0_.name_ = "in_flight";
  g0_.value_ = 3;
  g1_.name_ = "memory_bytes";
  g1_.value_ = 12345;
  snapshot_.gauges_.push_back(g0_);
  snapshot_.gauges_.push_back(g1_);

  auto ctx = buildContext(snapshot_);
  EXPECT_EQ(2u, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(&ctx));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeValid) {
  g0_.name_ = "in_flight";
  g0_.value_ = 7;
  snapshot_.gauges_.push_back(g0_);
  auto ctx = buildContext(snapshot_);

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(&ctx, 0, &name_out, &value_out));
  EXPECT_EQ("in_flight", toString(name_out));
  EXPECT_EQ(7u, value_out);
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetGaugeOutOfRange) {
  g0_.name_ = "only";
  g0_.value_ = 1;
  snapshot_.gauges_.push_back(g0_);
  auto ctx = buildContext(snapshot_);

  envoy_dynamic_module_type_envoy_buffer name_out{};
  uint64_t value_out = 0;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(&ctx, 1, &name_out, &value_out));
}

// =============================================================================
// Text readout callbacks
// =============================================================================

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutCountEmpty) {
  // MockMetricSnapshot doesn't wire textReadouts() by default, so do it here.
  ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));
  auto ctx = buildContext(snapshot_);
  EXPECT_EQ(0u, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(&ctx));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutCount) {
  t0_.name_ = "version";
  t0_.value_ = "1.0.0";
  t1_.name_ = "control_plane";
  t1_.value_ = "pilot-1";
  snapshot_.text_readouts_.push_back(t0_);
  snapshot_.text_readouts_.push_back(t1_);
  ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));

  auto ctx = buildContext(snapshot_);
  EXPECT_EQ(2u, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(&ctx));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutValid) {
  t0_.name_ = "version";
  t0_.value_ = "1.28.0";
  snapshot_.text_readouts_.push_back(t0_);
  ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));

  auto ctx = buildContext(snapshot_);
  envoy_dynamic_module_type_envoy_buffer name_out{};
  envoy_dynamic_module_type_envoy_buffer value_out{};
  EXPECT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(&ctx, 0, &name_out,
                                                                                &value_out));
  EXPECT_EQ("version", toString(name_out));
  EXPECT_EQ("1.28.0", toString(value_out));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutOutOfRange) {
  t0_.name_ = "only";
  t0_.value_ = "x";
  snapshot_.text_readouts_.push_back(t0_);
  ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));

  auto ctx = buildContext(snapshot_);
  envoy_dynamic_module_type_envoy_buffer name_out{};
  envoy_dynamic_module_type_envoy_buffer value_out{};
  EXPECT_FALSE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(&ctx, 1, &name_out,
                                                                                 &value_out));
}

TEST_F(DynamicModuleStatsSinkAbiTest, GetTextReadoutUsesCachedValue) {
  // Regression: TextReadout::value() returns std::string by value. Without the
  // pre-caching in the flush context, the pointer returned to the module would
  // dangle as soon as the temporary goes out of scope. Verify the returned
  // pointer aliases the context's cached value, not the mock's value_ member.
  t0_.name_ = "n";
  t0_.value_ = "cached_value";
  snapshot_.text_readouts_.push_back(t0_);
  ON_CALL(snapshot_, textReadouts()).WillByDefault(ReturnRef(snapshot_.text_readouts_));

  auto ctx = buildContext(snapshot_);
  envoy_dynamic_module_type_envoy_buffer name_out{};
  envoy_dynamic_module_type_envoy_buffer value_out{};
  ASSERT_TRUE(envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(&ctx, 0, &name_out,
                                                                                &value_out));
  EXPECT_EQ(name_out.ptr, ctx.text_readout_names[0].data());
  EXPECT_EQ(value_out.ptr, ctx.text_readout_values[0].data());
}

} // namespace
} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
