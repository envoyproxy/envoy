#include "test/mocks/stats/mocks.h"

#include "contrib/stat_sinks/wasm_filter/source/wasm_filter_stat_sink_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace WasmFilter {
namespace {

// ====================================================================
// Helpers
// ====================================================================

void appendU32(std::string& buf, uint32_t v) {
  buf.append(reinterpret_cast<const char*>(&v), sizeof(uint32_t));
}

void appendU64(std::string& buf, uint64_t v) {
  buf.append(reinterpret_cast<const char*>(&v), sizeof(uint64_t));
}

void appendStr(std::string& buf, const std::string& s) {
  appendU32(buf, s.size());
  buf.append(s);
}

proxy_wasm::WasmForeignFunction getFF(const std::string& name) {
  return proxy_wasm::getForeignFunction(name);
}

proxy_wasm::WasmResult callFF(const std::string& name, const std::string& args,
                              const std::function<void*(size_t)>& alloc = nullptr) {
  auto ff = getFF(name);
  if (ff == nullptr) {
    return proxy_wasm::WasmResult::NotFound;
  }
  proxy_wasm::WasmBase wasm_base(nullptr, "", "", "", {}, {});
  auto real_alloc = alloc ? alloc : [](size_t) -> void* { return nullptr; };
  return ff(wasm_base, args, real_alloc);
}

// ====================================================================
// Enriched Metric Snapshot fixture
// ====================================================================

class EnrichedMetricSnapshotTest : public testing::Test {
protected:
  void SetUp() override {
    counter_a_.name_ = "upstream_rq_2xx";
    counter_a_.latch_ = 10;
    counter_a_.used_ = true;
    counter_a_.setTags({{"envoy.cluster_name", "local_service"}});

    counter_b_.name_ = "upstream_rq_5xx";
    counter_b_.latch_ = 5;
    counter_b_.used_ = true;

    counter_c_.name_ = "upstream_rq_total";
    counter_c_.latch_ = 15;
    counter_c_.used_ = true;

    gauge_a_.name_ = "membership_total";
    gauge_a_.value_ = 100;
    gauge_a_.used_ = true;
    gauge_a_.setTags({{"envoy.cluster_name", "local_service"}});

    gauge_b_.name_ = "connections_active";
    gauge_b_.value_ = 42;
    gauge_b_.used_ = true;

    histogram_a_.name_ = "upstream_rq_time";
    histogram_a_.used_ = true;
    histogram_a_.setTags({{"envoy.cluster_name", "local_service"}});

    histogram_b_.name_ = "downstream_rq_time";
    histogram_b_.used_ = true;

    snapshot_.counters_ = {{10, counter_a_}, {5, counter_b_}, {15, counter_c_}};
    snapshot_.gauges_ = {gauge_a_, gauge_b_};
    snapshot_.histograms_ = {histogram_a_, histogram_b_};

    ON_CALL(snapshot_, counters()).WillByDefault(testing::ReturnRef(snapshot_.counters_));
    ON_CALL(snapshot_, gauges()).WillByDefault(testing::ReturnRef(snapshot_.gauges_));
    ON_CALL(snapshot_, histograms()).WillByDefault(testing::ReturnRef(snapshot_.histograms_));
    ON_CALL(snapshot_, textReadouts()).WillByDefault(testing::ReturnRef(snapshot_.text_readouts_));
    ON_CALL(snapshot_, hostCounters()).WillByDefault(testing::ReturnRef(snapshot_.host_counters_));
    ON_CALL(snapshot_, hostGauges()).WillByDefault(testing::ReturnRef(snapshot_.host_gauges_));
  }

  StatsFilterContext makeCtxKeepAll() {
    StatsFilterContext ctx;
    ctx.kept_counter_indices = {0, 1, 2};
    ctx.kept_gauge_indices = {0, 1};
    ctx.kept_histogram_indices = {0, 1};
    ctx.emit_called = true;
    ctx.histogram_block_present = true;
    ctx.snapshot = &snapshot_;
    return ctx;
  }

  NiceMock<Stats::MockCounter> counter_a_;
  NiceMock<Stats::MockCounter> counter_b_;
  NiceMock<Stats::MockCounter> counter_c_;
  NiceMock<Stats::MockGauge> gauge_a_;
  NiceMock<Stats::MockGauge> gauge_b_;
  NiceMock<Stats::MockParentHistogram> histogram_a_;
  NiceMock<Stats::MockParentHistogram> histogram_b_;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  Stats::TagVector empty_tags_;
  Stats::SymbolTable& symbol_table_{counter_a_.symbolTable()};
};

// ====================================================================
// Filtering tests
// ====================================================================

TEST_F(EnrichedMetricSnapshotTest, KeepAll) {
  auto ctx = makeCtxKeepAll();
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(enriched.counters().size(), 3);
  EXPECT_EQ(enriched.gauges().size(), 2);
  EXPECT_EQ(enriched.histograms().size(), 2);
}

TEST_F(EnrichedMetricSnapshotTest, DropAll) {
  StatsFilterContext ctx;
  ctx.kept_counter_indices = {};
  ctx.kept_gauge_indices = {};
  ctx.kept_histogram_indices = {999};
  ctx.emit_called = true;
  ctx.histogram_block_present = true;
  ctx.snapshot = &snapshot_;
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(enriched.counters().size(), 0);
  EXPECT_EQ(enriched.gauges().size(), 0);
  EXPECT_EQ(enriched.histograms().size(), 0);
}

TEST_F(EnrichedMetricSnapshotTest, KeepSubsetOfCounters) {
  StatsFilterContext ctx;
  ctx.kept_counter_indices = {0, 2};
  ctx.kept_gauge_indices = {0, 1};
  ctx.snapshot = &snapshot_;
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  ASSERT_EQ(enriched.counters().size(), 2);
  EXPECT_EQ(enriched.counters()[0].counter_.get().name(), "upstream_rq_2xx");
  EXPECT_EQ(enriched.counters()[0].delta_, 10);
  EXPECT_EQ(enriched.counters()[1].counter_.get().name(), "upstream_rq_total");
}

TEST_F(EnrichedMetricSnapshotTest, EmptyHistogramIndicesPassAllThrough) {
  StatsFilterContext ctx;
  ctx.kept_counter_indices = {0};
  ctx.kept_gauge_indices = {0};
  ctx.snapshot = &snapshot_;
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(enriched.histograms().size(), 2);
}

// ====================================================================
// Global tag injection tests
// ====================================================================

TEST_F(EnrichedMetricSnapshotTest, GlobalTagsAppliedToAllMetrics) {
  Stats::TagVector global_tags = {{"datacenter", "us-east-1"}, {"pod", "pod42"}};
  auto ctx = makeCtxKeepAll();
  EnrichedMetricSnapshot enriched(snapshot_, ctx, global_tags, symbol_table_);

  auto counter_tags = enriched.counters()[0].counter_.get().tags();
  EXPECT_GE(counter_tags.size(), 2);
  EXPECT_EQ(counter_tags[counter_tags.size() - 2].name_, "datacenter");
  EXPECT_EQ(counter_tags[counter_tags.size() - 2].value_, "us-east-1");
  EXPECT_EQ(counter_tags[counter_tags.size() - 1].name_, "pod");
  EXPECT_EQ(counter_tags[counter_tags.size() - 1].value_, "pod42");

  auto gauge_tags = enriched.gauges()[0].get().tags();
  EXPECT_GE(gauge_tags.size(), 2);
  EXPECT_EQ(gauge_tags[gauge_tags.size() - 1].name_, "pod");

  auto hist_tags = enriched.histograms()[0].get().tags();
  EXPECT_GE(hist_tags.size(), 2);
  EXPECT_EQ(hist_tags[hist_tags.size() - 1].name_, "pod");
}

// ====================================================================
// Name override tests
// ====================================================================

TEST_F(EnrichedMetricSnapshotTest, NameOverrideOnCounter) {
  auto ctx = makeCtxKeepAll();
  ctx.name_overrides.push_back({1, 0, "envoy.upstream_rq_2xx"});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(enriched.counters()[0].counter_.get().name(), "envoy.upstream_rq_2xx");
  EXPECT_EQ(enriched.counters()[1].counter_.get().name(), "upstream_rq_5xx");
}

TEST_F(EnrichedMetricSnapshotTest, NameOverrideOnGauge) {
  auto ctx = makeCtxKeepAll();
  ctx.name_overrides.push_back({2, 1, "envoy.connections_active"});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(enriched.gauges()[1].get().name(), "envoy.connections_active");
}

TEST_F(EnrichedMetricSnapshotTest, NameOverrideOnHistogram) {
  auto ctx = makeCtxKeepAll();
  ctx.name_overrides.push_back({3, 0, "envoy.upstream_rq_time"});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(enriched.histograms()[0].get().name(), "envoy.upstream_rq_time");
}

TEST_F(EnrichedMetricSnapshotTest, NameOverrideOutOfRange) {
  auto ctx = makeCtxKeepAll();
  ctx.name_overrides.push_back({1, 999, "out_of_range"});
  ctx.name_overrides.push_back({2, 999, "out_of_range"});
  ctx.name_overrides.push_back({3, 999, "out_of_range"});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(enriched.counters()[0].counter_.get().name(), "upstream_rq_2xx");
  EXPECT_EQ(enriched.gauges()[0].get().name(), "membership_total");
  EXPECT_EQ(enriched.histograms()[0].get().name(), "upstream_rq_time");
}

// ====================================================================
// Synthetic metric injection tests
// ====================================================================

TEST_F(EnrichedMetricSnapshotTest, SyntheticCounterInjected) {
  auto ctx = makeCtxKeepAll();
  ctx.synthetic_counters.push_back({"wasm_filter.metrics_emitted", 42, {}});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  ASSERT_EQ(enriched.counters().size(), 4);
  EXPECT_EQ(enriched.counters()[3].counter_.get().name(), "wasm_filter.metrics_emitted");
  EXPECT_EQ(enriched.counters()[3].counter_.get().value(), 42);
  EXPECT_EQ(enriched.counters()[3].delta_, 42);
}

TEST_F(EnrichedMetricSnapshotTest, SyntheticGaugeInjected) {
  auto ctx = makeCtxKeepAll();
  ctx.synthetic_gauges.push_back({"app.version", 1, {{"version", "v1.2.3"}}});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  ASSERT_EQ(enriched.gauges().size(), 3);
  EXPECT_EQ(enriched.gauges()[2].get().name(), "app.version");
  EXPECT_EQ(enriched.gauges()[2].get().value(), 1);
  auto tags = enriched.gauges()[2].get().tags();
  ASSERT_EQ(tags.size(), 1);
  EXPECT_EQ(tags[0].name_, "version");
  EXPECT_EQ(tags[0].value_, "v1.2.3");
}

TEST_F(EnrichedMetricSnapshotTest, SyntheticMetricsGetGlobalTags) {
  Stats::TagVector global_tags = {{"dc", "us-east-1"}};
  auto ctx = makeCtxKeepAll();
  ctx.synthetic_counters.push_back({"custom.count", 10, {}});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, global_tags, symbol_table_);

  auto last = enriched.counters().back();
  auto tags = last.counter_.get().tags();
  ASSERT_GE(tags.size(), 1);
  EXPECT_EQ(tags.back().name_, "dc");
}

// ====================================================================
// Snapshot forwarding tests
// ====================================================================

TEST_F(EnrichedMetricSnapshotTest, ForwardsTextReadoutsHostCountersHostGauges) {
  auto ctx = makeCtxKeepAll();
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  EXPECT_EQ(&enriched.textReadouts(), &snapshot_.text_readouts_);
  EXPECT_EQ(&enriched.hostCounters(), &snapshot_.host_counters_);
  EXPECT_EQ(&enriched.hostGauges(), &snapshot_.host_gauges_);
}

TEST_F(EnrichedMetricSnapshotTest, ForwardsSnapshotTime) {
  auto ctx = makeCtxKeepAll();
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);
  enriched.snapshotTime();
}

// ====================================================================
// Combined test
// ====================================================================

TEST_F(EnrichedMetricSnapshotTest, FilterRenameTagInjectCombined) {
  Stats::TagVector global_tags = {{"env", "prod"}};
  StatsFilterContext ctx;
  ctx.kept_counter_indices = {0};
  ctx.kept_gauge_indices = {1};
  ctx.snapshot = &snapshot_;
  ctx.name_overrides.push_back({1, 0, "envoy.upstream_rq_2xx"});
  ctx.synthetic_gauges.push_back({"wasm_filter.queue_depth", 7, {}});

  EnrichedMetricSnapshot enriched(snapshot_, ctx, global_tags, symbol_table_);

  ASSERT_EQ(enriched.counters().size(), 1);
  EXPECT_EQ(enriched.counters()[0].counter_.get().name(), "envoy.upstream_rq_2xx");
  auto ctags = enriched.counters()[0].counter_.get().tags();
  EXPECT_EQ(ctags.back().name_, "env");

  ASSERT_EQ(enriched.gauges().size(), 2);
  EXPECT_EQ(enriched.gauges()[0].get().name(), "connections_active");
  EXPECT_EQ(enriched.gauges()[1].get().name(), "wasm_filter.queue_depth");

  EXPECT_EQ(enriched.histograms().size(), 2);
}

// ====================================================================
// Enriched wrapper method tests
// ====================================================================

TEST_F(EnrichedMetricSnapshotTest, EnrichedCounterMethods) {
  auto ctx = makeCtxKeepAll();
  ctx.name_overrides.push_back({1, 0, "envoy.upstream_rq_2xx"});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& cref = enriched.counters()[0].counter_.get();
  EXPECT_EQ(cref.name(), "envoy.upstream_rq_2xx");
  EXPECT_EQ(cref.tagExtractedName(), "envoy.upstream_rq_2xx");
  EXPECT_TRUE(cref.used());
  EXPECT_FALSE(cref.hidden());
  EXPECT_EQ(cref.use_count(), 1);
  EXPECT_FALSE(cref.tags().empty());

  auto& mut_cref = const_cast<Stats::Counter&>(cref);
  mut_cref.add(0);
  mut_cref.inc();
  EXPECT_EQ(mut_cref.latch(), 0);
  mut_cref.reset();
  mut_cref.incRefCount();
  EXPECT_FALSE(mut_cref.decRefCount());
  mut_cref.markUnused();
}

TEST_F(EnrichedMetricSnapshotTest, EnrichedCounterOriginalName) {
  auto ctx = makeCtxKeepAll();
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& cref = enriched.counters()[0].counter_.get();
  EXPECT_EQ(cref.name(), "upstream_rq_2xx");
  EXPECT_EQ(cref.tagExtractedName(), "upstream_rq_2xx");
}

TEST_F(EnrichedMetricSnapshotTest, EnrichedGaugeMethods) {
  auto ctx = makeCtxKeepAll();
  ctx.name_overrides.push_back({2, 0, "envoy.membership"});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& gref = enriched.gauges()[0].get();
  EXPECT_EQ(gref.name(), "envoy.membership");
  EXPECT_EQ(gref.tagExtractedName(), "envoy.membership");
  EXPECT_EQ(gref.value(), 100);
  EXPECT_TRUE(gref.used());
  EXPECT_FALSE(gref.hidden());
  EXPECT_EQ(gref.use_count(), 1);
  EXPECT_FALSE(gref.tags().empty());

  auto& mut_gref = const_cast<Stats::Gauge&>(gref);
  mut_gref.add(0);
  mut_gref.dec();
  mut_gref.inc();
  mut_gref.set(0);
  mut_gref.sub(0);
  mut_gref.setParentValue(0);
  mut_gref.importMode();
  mut_gref.mergeImportMode(Stats::Gauge::ImportMode::NeverImport);
  mut_gref.incRefCount();
  EXPECT_FALSE(mut_gref.decRefCount());
  mut_gref.markUnused();
}

TEST_F(EnrichedMetricSnapshotTest, EnrichedGaugeOriginalName) {
  auto ctx = makeCtxKeepAll();
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& gref = enriched.gauges()[0].get();
  EXPECT_EQ(gref.name(), "membership_total");
  EXPECT_EQ(gref.tagExtractedName(), "membership_total");
}

TEST_F(EnrichedMetricSnapshotTest, EnrichedHistogramMethods) {
  auto ctx = makeCtxKeepAll();
  ctx.name_overrides.push_back({3, 0, "envoy.upstream_rq_time"});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& href = enriched.histograms()[0].get();
  EXPECT_EQ(href.name(), "envoy.upstream_rq_time");
  EXPECT_EQ(href.tagExtractedName(), "envoy.upstream_rq_time");
  EXPECT_TRUE(href.used());
  EXPECT_FALSE(href.hidden());
  EXPECT_EQ(href.use_count(), 1);
  EXPECT_FALSE(href.tags().empty());
  href.unit();
  href.quantileSummary();
  href.bucketSummary();
  href.detailedTotalBuckets();
  href.detailedIntervalBuckets();
  href.cumulativeCountLessThanOrEqualToValue(1.0);
  href.intervalStatistics();
  href.cumulativeStatistics();

  auto& mut_href = const_cast<Stats::ParentHistogram&>(href);
  mut_href.recordValue(0);
  mut_href.merge();
  mut_href.incRefCount();
  EXPECT_FALSE(mut_href.decRefCount());
  mut_href.markUnused();
}

TEST_F(EnrichedMetricSnapshotTest, EnrichedHistogramOriginalName) {
  auto ctx = makeCtxKeepAll();
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& href = enriched.histograms()[0].get();
  EXPECT_EQ(href.name(), "upstream_rq_time");
  EXPECT_EQ(href.tagExtractedName(), "upstream_rq_time");
}

TEST_F(EnrichedMetricSnapshotTest, SyntheticCounterMethods) {
  auto ctx = makeCtxKeepAll();
  ctx.synthetic_counters.push_back({"syn.count", 77, {{"k", "v"}}});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& sc = enriched.counters().back().counter_.get();
  EXPECT_EQ(sc.name(), "syn.count");
  EXPECT_EQ(sc.tagExtractedName(), "syn.count");
  EXPECT_EQ(sc.value(), 77);
  EXPECT_TRUE(sc.used());
  EXPECT_FALSE(sc.hidden());
  EXPECT_EQ(sc.use_count(), 1);
  EXPECT_EQ(sc.tags().size(), 1);
  sc.constSymbolTable();

  auto& mut_sc = const_cast<Stats::Counter&>(sc);
  mut_sc.add(0);
  mut_sc.inc();
  EXPECT_EQ(mut_sc.latch(), 0);
  mut_sc.reset();
  mut_sc.symbolTable();
  mut_sc.incRefCount();
  EXPECT_FALSE(mut_sc.decRefCount());
  mut_sc.markUnused();
  mut_sc.iterateTagStatNames([](Stats::StatName, Stats::StatName) { return true; });
}

TEST_F(EnrichedMetricSnapshotTest, SyntheticGaugeMethods) {
  auto ctx = makeCtxKeepAll();
  ctx.synthetic_gauges.push_back({"syn.gauge", 33, {{"x", "y"}}});
  EnrichedMetricSnapshot enriched(snapshot_, ctx, empty_tags_, symbol_table_);

  const auto& sg = enriched.gauges().back().get();
  EXPECT_EQ(sg.name(), "syn.gauge");
  EXPECT_EQ(sg.tagExtractedName(), "syn.gauge");
  EXPECT_EQ(sg.value(), 33);
  EXPECT_TRUE(sg.used());
  EXPECT_FALSE(sg.hidden());
  EXPECT_EQ(sg.use_count(), 1);
  EXPECT_EQ(sg.tags().size(), 1);
  sg.constSymbolTable();
  EXPECT_EQ(sg.importMode(), Stats::Gauge::ImportMode::NeverImport);

  auto& mut_sg = const_cast<Stats::Gauge&>(sg);
  mut_sg.add(0);
  mut_sg.dec();
  mut_sg.inc();
  mut_sg.set(0);
  mut_sg.sub(0);
  mut_sg.setParentValue(0);
  mut_sg.mergeImportMode(Stats::Gauge::ImportMode::NeverImport);
  mut_sg.symbolTable();
  mut_sg.incRefCount();
  EXPECT_FALSE(mut_sg.decRefCount());
  mut_sg.markUnused();
  mut_sg.iterateTagStatNames([](Stats::StatName, Stats::StatName) { return true; });
}

// ====================================================================
// MergeTags utility
// ====================================================================

TEST(MergeTagsTest, EmptyExtraReturnsOriginal) {
  Stats::TagVector original = {{"a", "1"}};
  Stats::TagVector extra = {};
  auto merged = mergeTags(original, extra);
  ASSERT_EQ(merged.size(), 1);
  EXPECT_EQ(merged[0].name_, "a");
}

TEST(MergeTagsTest, MergesBothVectors) {
  Stats::TagVector original = {{"a", "1"}};
  Stats::TagVector extra = {{"b", "2"}, {"c", "3"}};
  auto merged = mergeTags(original, extra);
  ASSERT_EQ(merged.size(), 3);
  EXPECT_EQ(merged[0].name_, "a");
  EXPECT_EQ(merged[1].name_, "b");
  EXPECT_EQ(merged[2].name_, "c");
}

// ====================================================================
// StatsFilterContext tests
// ====================================================================

TEST(StatsFilterContextAccessorTest, ThreadLocalAccessors) {
  EXPECT_EQ(getActiveContext(), nullptr);

  StatsFilterContext ctx;
  setActiveContext(&ctx);
  EXPECT_EQ(getActiveContext(), &ctx);

  setActiveContext(nullptr);
  EXPECT_EQ(getActiveContext(), nullptr);
}

TEST(StatsFilterContextAccessorTest, GlobalTagsAccessors) {
  EXPECT_EQ(getGlobalTags(), nullptr);

  Stats::TagVector tags = {{"k", "v"}};
  setGlobalTags(&tags);
  EXPECT_EQ(getGlobalTags(), &tags);

  setGlobalTags(nullptr);
  EXPECT_EQ(getGlobalTags(), nullptr);
}

TEST(StatsFilterContextAccessorTest, ClearResetsEverything) {
  StatsFilterContext ctx;
  ctx.kept_counter_indices.insert(1);
  ctx.kept_gauge_indices.insert(2);
  ctx.kept_histogram_indices.insert(3);
  ctx.emit_called = true;
  ctx.histogram_block_present = true;
  ctx.name_overrides.push_back({1, 0, "foo"});
  ctx.synthetic_counters.push_back({"bar", 1, {}});
  ctx.synthetic_gauges.push_back({"baz", 2, {}});
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  NiceMock<Stats::MockMetricSnapshot> snap;
  ctx.snapshot = &snap;

  ctx.clear();

  EXPECT_TRUE(ctx.kept_counter_indices.empty());
  EXPECT_TRUE(ctx.kept_gauge_indices.empty());
  EXPECT_TRUE(ctx.kept_histogram_indices.empty());
  EXPECT_FALSE(ctx.emit_called);
  EXPECT_FALSE(ctx.histogram_block_present);
  EXPECT_TRUE(ctx.name_overrides.empty());
  EXPECT_TRUE(ctx.synthetic_counters.empty());
  EXPECT_TRUE(ctx.synthetic_gauges.empty());
  EXPECT_TRUE(ctx.counter_buffer_to_snapshot.empty());
  EXPECT_TRUE(ctx.gauge_buffer_to_snapshot.empty());
  EXPECT_EQ(ctx.snapshot, nullptr);
}

// ====================================================================
// Foreign function: stats_filter_emit
// ====================================================================

TEST(StatsFilterEmitTest, ParseCountersGaugesAndHistograms) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::vector<uint32_t> wire = {2, 0, 2, 1, 1, 1, 0};
  std::string arguments(reinterpret_cast<const char*>(wire.data()), wire.size() * sizeof(uint32_t));

  EXPECT_EQ(callFF("stats_filter_emit", arguments), proxy_wasm::WasmResult::Ok);

  EXPECT_EQ(ctx.kept_counter_indices.size(), 2);
  EXPECT_TRUE(ctx.kept_counter_indices.contains(0));
  EXPECT_TRUE(ctx.kept_counter_indices.contains(2));
  EXPECT_EQ(ctx.kept_gauge_indices.size(), 1);
  EXPECT_TRUE(ctx.kept_gauge_indices.contains(1));
  EXPECT_EQ(ctx.kept_histogram_indices.size(), 1);
  EXPECT_TRUE(ctx.kept_histogram_indices.contains(0));
  EXPECT_TRUE(ctx.emit_called);
  EXPECT_TRUE(ctx.histogram_block_present);

  setActiveContext(nullptr);
}

TEST(StatsFilterEmitTest, HistogramBlockIsOptional) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::vector<uint32_t> wire = {1, 0, 0};
  std::string arguments(reinterpret_cast<const char*>(wire.data()), wire.size() * sizeof(uint32_t));

  EXPECT_EQ(callFF("stats_filter_emit", arguments), proxy_wasm::WasmResult::Ok);
  EXPECT_EQ(ctx.kept_counter_indices.size(), 1);
  EXPECT_TRUE(ctx.kept_histogram_indices.empty());
  EXPECT_TRUE(ctx.emit_called);
  EXPECT_FALSE(ctx.histogram_block_present);

  setActiveContext(nullptr);
}

TEST(StatsFilterEmitTest, NoActiveContext) {
  setActiveContext(nullptr);

  std::vector<uint32_t> wire = {0, 0};
  std::string arguments(reinterpret_cast<const char*>(wire.data()), wire.size() * sizeof(uint32_t));

  EXPECT_EQ(callFF("stats_filter_emit", arguments), proxy_wasm::WasmResult::InternalFailure);
}

TEST(StatsFilterEmitTest, EmptyArguments) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);
  EXPECT_EQ(callFF("stats_filter_emit", ""), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

TEST(StatsFilterEmitTest, TruncatedCounterBlock) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 5);
  appendU32(wire, 0);

  EXPECT_EQ(callFF("stats_filter_emit", wire), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

TEST(StatsFilterEmitTest, TruncatedGaugeBlock) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 0);
  appendU32(wire, 10);
  appendU32(wire, 0);

  EXPECT_EQ(callFF("stats_filter_emit", wire), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

// ====================================================================
// Foreign function: stats_filter_set_global_tags
// ====================================================================

TEST(StatsFilterGlobalTagsTest, SetGlobalTagsWithActivePointer) {
  Stats::TagVector tags;
  setGlobalTags(&tags);

  std::string wire;
  appendU32(wire, 2);
  appendStr(wire, "dc");
  appendStr(wire, "us");
  appendStr(wire, "pod");
  appendStr(wire, "p42");

  EXPECT_EQ(callFF("stats_filter_set_global_tags", wire), proxy_wasm::WasmResult::Ok);

  ASSERT_EQ(tags.size(), 2);
  EXPECT_EQ(tags[0].name_, "dc");
  EXPECT_EQ(tags[0].value_, "us");
  EXPECT_EQ(tags[1].name_, "pod");
  EXPECT_EQ(tags[1].value_, "p42");

  setGlobalTags(nullptr);
}

TEST(StatsFilterGlobalTagsTest, SetGlobalTagsNullPointerReturnsFailure) {
  setGlobalTags(nullptr);

  std::string wire;
  appendU32(wire, 1);
  appendStr(wire, "env");
  appendStr(wire, "staging");

  EXPECT_EQ(callFF("stats_filter_set_global_tags", wire), proxy_wasm::WasmResult::InternalFailure);
}

TEST(StatsFilterGlobalTagsTest, SetGlobalTagsBadArgument) {
  Stats::TagVector tags;
  setGlobalTags(&tags);

  std::string wire;
  appendU32(wire, 1);
  appendStr(wire, "key");

  EXPECT_EQ(callFF("stats_filter_set_global_tags", wire), proxy_wasm::WasmResult::BadArgument);
  setGlobalTags(nullptr);
}

TEST(StatsFilterGlobalTagsTest, ActivePointerClearsBeforeReuse) {
  Stats::TagVector tags = {{"old", "val"}};
  setGlobalTags(&tags);

  std::string wire;
  appendU32(wire, 1);
  appendStr(wire, "new");
  appendStr(wire, "val");

  EXPECT_EQ(callFF("stats_filter_set_global_tags", wire), proxy_wasm::WasmResult::Ok);
  ASSERT_EQ(tags.size(), 1);
  EXPECT_EQ(tags[0].name_, "new");

  setGlobalTags(nullptr);
}

// ====================================================================
// Foreign function: stats_filter_set_name_overrides
// ====================================================================

TEST(StatsFilterNameOverridesTest, SetNameOverrides) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 1);
  appendU32(wire, 1);
  appendU32(wire, 0);
  appendStr(wire, "envoy.rq");

  EXPECT_EQ(callFF("stats_filter_set_name_overrides", wire), proxy_wasm::WasmResult::Ok);

  ASSERT_EQ(ctx.name_overrides.size(), 1);
  EXPECT_EQ(ctx.name_overrides[0].type, 1);
  EXPECT_EQ(ctx.name_overrides[0].index, 0);
  EXPECT_EQ(ctx.name_overrides[0].new_name, "envoy.rq");

  setActiveContext(nullptr);
}

TEST(StatsFilterNameOverridesTest, NoActiveContext) {
  setActiveContext(nullptr);
  std::string wire;
  appendU32(wire, 0);
  EXPECT_EQ(callFF("stats_filter_set_name_overrides", wire),
            proxy_wasm::WasmResult::InternalFailure);
}

TEST(StatsFilterNameOverridesTest, BadArguments) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);
  EXPECT_EQ(callFF("stats_filter_set_name_overrides", ""), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

TEST(StatsFilterNameOverridesTest, TruncatedOverride) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 1);
  appendU32(wire, 1);

  EXPECT_EQ(callFF("stats_filter_set_name_overrides", wire), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

TEST(StatsFilterNameOverridesTest, TruncatedName) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 1);
  appendU32(wire, 1);
  appendU32(wire, 0);
  appendU32(wire, 100);

  EXPECT_EQ(callFF("stats_filter_set_name_overrides", wire), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

// ====================================================================
// Foreign function: stats_filter_inject_metrics
// ====================================================================

TEST(StatsFilterInjectMetricsTest, InjectCounterAndGauge) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 1);
  appendStr(wire, "custom.count");
  appendU64(wire, 99);
  appendU32(wire, 0);
  appendU32(wire, 1);
  appendStr(wire, "custom.gauge");
  appendU64(wire, 7);
  appendU32(wire, 1);
  appendStr(wire, "k");
  appendStr(wire, "v");

  EXPECT_EQ(callFF("stats_filter_inject_metrics", wire), proxy_wasm::WasmResult::Ok);

  ASSERT_EQ(ctx.synthetic_counters.size(), 1);
  EXPECT_EQ(ctx.synthetic_counters[0].name, "custom.count");
  EXPECT_EQ(ctx.synthetic_counters[0].value, 99);

  ASSERT_EQ(ctx.synthetic_gauges.size(), 1);
  EXPECT_EQ(ctx.synthetic_gauges[0].name, "custom.gauge");
  EXPECT_EQ(ctx.synthetic_gauges[0].value, 7);
  ASSERT_EQ(ctx.synthetic_gauges[0].tags.size(), 1);
  EXPECT_EQ(ctx.synthetic_gauges[0].tags[0].name_, "k");
  EXPECT_EQ(ctx.synthetic_gauges[0].tags[0].value_, "v");

  setActiveContext(nullptr);
}

TEST(StatsFilterInjectMetricsTest, NoActiveContext) {
  setActiveContext(nullptr);
  std::string wire;
  appendU32(wire, 0);
  appendU32(wire, 0);
  EXPECT_EQ(callFF("stats_filter_inject_metrics", wire), proxy_wasm::WasmResult::InternalFailure);
}

TEST(StatsFilterInjectMetricsTest, TruncatedCounterBlock) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 1);
  appendStr(wire, "name");

  EXPECT_EQ(callFF("stats_filter_inject_metrics", wire), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

TEST(StatsFilterInjectMetricsTest, TruncatedGaugeBlock) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);

  std::string wire;
  appendU32(wire, 0);
  appendU32(wire, 1);
  appendStr(wire, "name");

  EXPECT_EQ(callFF("stats_filter_inject_metrics", wire), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

TEST(StatsFilterInjectMetricsTest, EmptyInput) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);
  EXPECT_EQ(callFF("stats_filter_inject_metrics", ""), proxy_wasm::WasmResult::BadArgument);
  setActiveContext(nullptr);
}

// ====================================================================
// Foreign function: stats_filter_get_metric_tags
// ====================================================================

class GetMetricTagsTest : public EnrichedMetricSnapshotTest {
protected:
  void SetUp() override {
    EnrichedMetricSnapshotTest::SetUp();
    ctx_.snapshot = &snapshot_;
    ctx_.counter_buffer_to_snapshot = {0, 1, 2};
    ctx_.gauge_buffer_to_snapshot = {0, 1};
    setActiveContext(&ctx_);
  }
  void TearDown() override { setActiveContext(nullptr); }

  StatsFilterContext ctx_;
};

TEST_F(GetMetricTagsTest, GetCounterTags) {
  std::string wire;
  appendU32(wire, 1);
  appendU32(wire, 0);

  std::string result_buf;
  auto alloc = [&result_buf](size_t size) -> void* {
    result_buf.resize(size);
    return result_buf.data();
  };

  auto ff = getFF("stats_filter_get_metric_tags");
  ASSERT_TRUE(ff != nullptr);
  proxy_wasm::WasmBase wasm_base(nullptr, "", "", "", {}, {});
  auto r = ff(wasm_base, wire, alloc);
  EXPECT_EQ(r, proxy_wasm::WasmResult::Ok);
  EXPECT_GT(result_buf.size(), sizeof(uint32_t));

  uint32_t tag_count = 0;
  memcpy(&tag_count, result_buf.data(), sizeof(uint32_t));
  EXPECT_EQ(tag_count, 1);
}

TEST_F(GetMetricTagsTest, GetGaugeTags) {
  std::string wire;
  appendU32(wire, 2);
  appendU32(wire, 0);

  std::string result_buf;
  auto alloc = [&result_buf](size_t size) -> void* {
    result_buf.resize(size);
    return result_buf.data();
  };

  auto ff = getFF("stats_filter_get_metric_tags");
  proxy_wasm::WasmBase wasm_base(nullptr, "", "", "", {}, {});
  auto r = ff(wasm_base, wire, alloc);
  EXPECT_EQ(r, proxy_wasm::WasmResult::Ok);

  uint32_t tag_count = 0;
  memcpy(&tag_count, result_buf.data(), sizeof(uint32_t));
  EXPECT_EQ(tag_count, 1);
}

TEST_F(GetMetricTagsTest, GetHistogramTags) {
  std::string wire;
  appendU32(wire, 3);
  appendU32(wire, 0);

  std::string result_buf;
  auto alloc = [&result_buf](size_t size) -> void* {
    result_buf.resize(size);
    return result_buf.data();
  };

  auto ff = getFF("stats_filter_get_metric_tags");
  proxy_wasm::WasmBase wasm_base(nullptr, "", "", "", {}, {});
  auto r = ff(wasm_base, wire, alloc);
  EXPECT_EQ(r, proxy_wasm::WasmResult::Ok);

  uint32_t tag_count = 0;
  memcpy(&tag_count, result_buf.data(), sizeof(uint32_t));
  EXPECT_EQ(tag_count, 1);
}

TEST_F(GetMetricTagsTest, InvalidType) {
  std::string wire;
  appendU32(wire, 99);
  appendU32(wire, 0);

  EXPECT_EQ(callFF("stats_filter_get_metric_tags", wire), proxy_wasm::WasmResult::BadArgument);
}

TEST_F(GetMetricTagsTest, CounterIndexOutOfRange) {
  std::string wire;
  appendU32(wire, 1);
  appendU32(wire, 999);

  EXPECT_EQ(callFF("stats_filter_get_metric_tags", wire), proxy_wasm::WasmResult::BadArgument);
}

TEST_F(GetMetricTagsTest, GaugeIndexOutOfRange) {
  std::string wire;
  appendU32(wire, 2);
  appendU32(wire, 999);

  EXPECT_EQ(callFF("stats_filter_get_metric_tags", wire), proxy_wasm::WasmResult::BadArgument);
}

TEST_F(GetMetricTagsTest, HistogramIndexOutOfRange) {
  std::string wire;
  appendU32(wire, 3);
  appendU32(wire, 999);

  EXPECT_EQ(callFF("stats_filter_get_metric_tags", wire), proxy_wasm::WasmResult::BadArgument);
}

TEST_F(GetMetricTagsTest, TooShortArguments) {
  std::string wire;
  appendU32(wire, 1);

  EXPECT_EQ(callFF("stats_filter_get_metric_tags", wire), proxy_wasm::WasmResult::BadArgument);
}

TEST(StatsFilterGetMetricTagsTest, NoActiveContext) {
  setActiveContext(nullptr);
  std::string wire;
  appendU32(wire, 1);
  appendU32(wire, 0);
  EXPECT_EQ(callFF("stats_filter_get_metric_tags", wire), proxy_wasm::WasmResult::InternalFailure);
}

TEST(StatsFilterGetMetricTagsTest, NullSnapshot) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);
  std::string wire;
  appendU32(wire, 1);
  appendU32(wire, 0);
  EXPECT_EQ(callFF("stats_filter_get_metric_tags", wire), proxy_wasm::WasmResult::InternalFailure);
  setActiveContext(nullptr);
}

// ====================================================================
// Foreign function: stats_filter_get_all_metric_tags
// ====================================================================

class GetAllMetricTagsTest : public EnrichedMetricSnapshotTest {
protected:
  void SetUp() override {
    EnrichedMetricSnapshotTest::SetUp();
    ctx_.snapshot = &snapshot_;
    buildBufferToSnapshotMaps(snapshot_, ctx_);
    setActiveContext(&ctx_);
  }
  void TearDown() override { setActiveContext(nullptr); }

  StatsFilterContext ctx_;
};

TEST_F(GetAllMetricTagsTest, ReturnsAllTags) {
  std::string result_buf;
  auto alloc = [&result_buf](size_t size) -> void* {
    result_buf.resize(size);
    return result_buf.data();
  };

  auto ff = getFF("stats_filter_get_all_metric_tags");
  ASSERT_TRUE(ff != nullptr);
  proxy_wasm::WasmBase wasm_base(nullptr, "", "", "", {}, {});
  auto r = ff(wasm_base, "", alloc);
  EXPECT_EQ(r, proxy_wasm::WasmResult::Ok);
  EXPECT_GT(result_buf.size(), 3 * sizeof(uint32_t));

  size_t offset = 0;
  auto readU32 = [&](uint32_t& out) {
    memcpy(&out, result_buf.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
  };

  uint32_t counter_count = 0;
  readU32(counter_count);
  EXPECT_EQ(counter_count, 3);

  for (uint32_t i = 0; i < counter_count; ++i) {
    uint32_t tc = 0;
    readU32(tc);
    for (uint32_t t = 0; t < tc; ++t) {
      uint32_t nlen = 0;
      readU32(nlen);
      offset += nlen;
      uint32_t vlen = 0;
      readU32(vlen);
      offset += vlen;
    }
  }

  uint32_t gauge_count = 0;
  readU32(gauge_count);
  EXPECT_EQ(gauge_count, 2);
}

TEST(StatsFilterGetAllMetricTagsTest, NoActiveContext) {
  setActiveContext(nullptr);
  EXPECT_EQ(callFF("stats_filter_get_all_metric_tags", ""),
            proxy_wasm::WasmResult::InternalFailure);
}

TEST(StatsFilterGetAllMetricTagsTest, NullSnapshot) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);
  EXPECT_EQ(callFF("stats_filter_get_all_metric_tags", ""),
            proxy_wasm::WasmResult::InternalFailure);
  setActiveContext(nullptr);
}

// ====================================================================
// Foreign function: stats_filter_get_histograms
// ====================================================================

class GetHistogramsTest : public EnrichedMetricSnapshotTest {
protected:
  void SetUp() override {
    EnrichedMetricSnapshotTest::SetUp();
    ctx_.snapshot = &snapshot_;
    setActiveContext(&ctx_);
  }
  void TearDown() override { setActiveContext(nullptr); }

  StatsFilterContext ctx_;
};

TEST_F(GetHistogramsTest, ReturnsHistogramNames) {
  std::string result_buf;
  auto alloc = [&result_buf](size_t size) -> void* {
    result_buf.resize(size);
    return result_buf.data();
  };

  auto ff = getFF("stats_filter_get_histograms");
  ASSERT_TRUE(ff != nullptr);
  proxy_wasm::WasmBase wasm_base(nullptr, "", "", "", {}, {});
  auto r = ff(wasm_base, "", alloc);
  EXPECT_EQ(r, proxy_wasm::WasmResult::Ok);

  size_t offset = 0;
  uint32_t count = 0;
  memcpy(&count, result_buf.data() + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  EXPECT_EQ(count, 2);

  uint32_t name_len = 0;
  memcpy(&name_len, result_buf.data() + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  std::string name1(result_buf.data() + offset, name_len);
  offset += name_len;
  EXPECT_EQ(name1, "upstream_rq_time");

  memcpy(&name_len, result_buf.data() + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  std::string name2(result_buf.data() + offset, name_len);
  EXPECT_EQ(name2, "downstream_rq_time");
}

TEST(StatsFilterGetHistogramsTest, NoActiveContext) {
  setActiveContext(nullptr);
  EXPECT_EQ(callFF("stats_filter_get_histograms", ""), proxy_wasm::WasmResult::InternalFailure);
}

TEST(StatsFilterGetHistogramsTest, NullSnapshot) {
  StatsFilterContext ctx;
  setActiveContext(&ctx);
  EXPECT_EQ(callFF("stats_filter_get_histograms", ""), proxy_wasm::WasmResult::InternalFailure);
  setActiveContext(nullptr);
}

// ====================================================================
// Foreign function registration existence
// ====================================================================

TEST(StatsFilterForeignFunctionRegistration, AllFunctionsRegistered) {
  EXPECT_NE(getFF("stats_filter_emit"), nullptr);
  EXPECT_NE(getFF("stats_filter_set_global_tags"), nullptr);
  EXPECT_NE(getFF("stats_filter_set_name_overrides"), nullptr);
  EXPECT_NE(getFF("stats_filter_inject_metrics"), nullptr);
  EXPECT_NE(getFF("stats_filter_get_metric_tags"), nullptr);
  EXPECT_NE(getFF("stats_filter_get_all_metric_tags"), nullptr);
  EXPECT_NE(getFF("stats_filter_get_histograms"), nullptr);
}

// ====================================================================
// buildBufferToSnapshotMaps
// ====================================================================

class BuildBufferToSnapshotMapsTest : public EnrichedMetricSnapshotTest {};

TEST_F(BuildBufferToSnapshotMapsTest, AllUsedMetrics) {
  StatsFilterContext ctx;
  buildBufferToSnapshotMaps(snapshot_, ctx);

  ASSERT_EQ(ctx.counter_buffer_to_snapshot.size(), 3);
  EXPECT_EQ(ctx.counter_buffer_to_snapshot[0], 0);
  EXPECT_EQ(ctx.counter_buffer_to_snapshot[1], 1);
  EXPECT_EQ(ctx.counter_buffer_to_snapshot[2], 2);

  ASSERT_EQ(ctx.gauge_buffer_to_snapshot.size(), 2);
  EXPECT_EQ(ctx.gauge_buffer_to_snapshot[0], 0);
  EXPECT_EQ(ctx.gauge_buffer_to_snapshot[1], 1);
}

TEST_F(BuildBufferToSnapshotMapsTest, MixedUsedAndUnused) {
  counter_b_.used_ = false;
  gauge_b_.used_ = false;

  StatsFilterContext ctx;
  buildBufferToSnapshotMaps(snapshot_, ctx);

  ASSERT_EQ(ctx.counter_buffer_to_snapshot.size(), 2);
  EXPECT_EQ(ctx.counter_buffer_to_snapshot[0], 0);
  EXPECT_EQ(ctx.counter_buffer_to_snapshot[1], 2);

  ASSERT_EQ(ctx.gauge_buffer_to_snapshot.size(), 1);
  EXPECT_EQ(ctx.gauge_buffer_to_snapshot[0], 0);
}

TEST_F(BuildBufferToSnapshotMapsTest, NoneUsed) {
  counter_a_.used_ = false;
  counter_b_.used_ = false;
  counter_c_.used_ = false;
  gauge_a_.used_ = false;
  gauge_b_.used_ = false;

  StatsFilterContext ctx;
  buildBufferToSnapshotMaps(snapshot_, ctx);

  EXPECT_TRUE(ctx.counter_buffer_to_snapshot.empty());
  EXPECT_TRUE(ctx.gauge_buffer_to_snapshot.empty());
}

// ====================================================================
// processFilterDecisionsAndFlush
// ====================================================================

class ProcessFilterDecisionsTest : public EnrichedMetricSnapshotTest {
protected:
  NiceMock<Stats::MockSink> inner_sink_;
};

TEST_F(ProcessFilterDecisionsTest, NoDecisionsNoEnrichments_Passthrough) {
  StatsFilterContext ctx;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::Ref(snapshot_)));
  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, GlobalTagsOnly_KeepAllAndEnrich) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  Stats::TagVector global_tags = {{"dc", "us-east-1"}};

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 3);
        EXPECT_EQ(s.gauges().size(), 2);
        auto tags = s.counters()[0].counter_.get().tags();
        EXPECT_EQ(tags.back().name_, "dc");
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, FilterDecisions_KeepSubset) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.kept_counter_indices = {0};
  ctx.kept_gauge_indices = {1};
  ctx.emit_called = true;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 1);
        EXPECT_EQ(s.counters()[0].counter_.get().name(), "upstream_rq_2xx");
        EXPECT_EQ(s.gauges().size(), 1);
        EXPECT_EQ(s.gauges()[0].get().name(), "connections_active");
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, FilterDecisions_UnusedMetricsPassThrough) {
  counter_c_.used_ = false;
  gauge_b_.used_ = false;

  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1};
  ctx.gauge_buffer_to_snapshot = {0};
  ctx.kept_counter_indices = {0};
  ctx.kept_gauge_indices = {0};
  ctx.emit_called = true;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 2);
        EXPECT_EQ(s.gauges().size(), 2);
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, IndexTranslation) {
  counter_b_.used_ = false;

  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.kept_counter_indices = {1};
  ctx.kept_gauge_indices = {0, 1};
  ctx.emit_called = true;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        bool found_total = false;
        bool found_5xx = false;
        for (const auto& c : s.counters()) {
          if (c.counter_.get().name() == "upstream_rq_total") {
            found_total = true;
          }
          if (c.counter_.get().name() == "upstream_rq_5xx") {
            found_5xx = true;
          }
        }
        EXPECT_TRUE(found_total);
        EXPECT_TRUE(found_5xx);
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, NameOverrideTranslation) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.kept_counter_indices = {0, 1, 2};
  ctx.kept_gauge_indices = {0, 1};
  ctx.emit_called = true;
  ctx.name_overrides.push_back({1, 0, "envoy.rq_2xx"});
  ctx.name_overrides.push_back({2, 0, "envoy.membership"});
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters()[0].counter_.get().name(), "envoy.rq_2xx");
        EXPECT_EQ(s.gauges()[0].get().name(), "envoy.membership");
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, SyntheticMetrics_NoFilterDecisions) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.synthetic_counters.push_back({"custom.metric", 100, {}});
  ctx.synthetic_gauges.push_back({"custom.gauge", 50, {}});
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 4);
        EXPECT_EQ(s.counters().back().counter_.get().name(), "custom.metric");
        EXPECT_EQ(s.gauges().size(), 3);
        EXPECT_EQ(s.gauges().back().get().name(), "custom.gauge");
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, HistogramFilterDecisions) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.kept_counter_indices = {0, 1, 2};
  ctx.kept_gauge_indices = {0, 1};
  ctx.kept_histogram_indices = {0};
  ctx.emit_called = true;
  ctx.histogram_block_present = true;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.histograms().size(), 1);
        EXPECT_EQ(s.histograms()[0].get().name(), "upstream_rq_time");
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, OutOfRangeBufferIndicesIgnored) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0};
  ctx.gauge_buffer_to_snapshot = {0};
  ctx.kept_counter_indices = {0, 99};
  ctx.kept_gauge_indices = {0, 99};
  ctx.emit_called = true;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_));
  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, NameOverrideOutOfRangeMapping) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0};
  ctx.gauge_buffer_to_snapshot = {0};
  ctx.kept_counter_indices = {0};
  ctx.kept_gauge_indices = {0};
  ctx.emit_called = true;
  ctx.name_overrides.push_back({1, 99, "wont_apply"});
  ctx.name_overrides.push_back({2, 99, "wont_apply"});
  ctx.name_overrides.push_back({3, 0, "histogram_rename"});
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_));
  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, CombinedFilterEnrichSyntheticOverride) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.kept_counter_indices = {0, 2};
  ctx.kept_gauge_indices = {0};
  ctx.emit_called = true;
  ctx.name_overrides.push_back({1, 0, "envoy.rq_2xx"});
  ctx.synthetic_counters.push_back({"wasm.count", 5, {{"source", "plugin"}}});
  Stats::TagVector global_tags = {{"env", "prod"}};

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 3);
        EXPECT_EQ(s.counters()[0].counter_.get().name(), "envoy.rq_2xx");
        auto tags = s.counters()[0].counter_.get().tags();
        EXPECT_EQ(tags.back().name_, "env");

        EXPECT_EQ(s.counters().back().counter_.get().name(), "wasm.count");
        auto syn_tags = s.counters().back().counter_.get().tags();
        EXPECT_GE(syn_tags.size(), 2);
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, EmitCalledWithEmptySets_DropsAllUsedMetrics) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.emit_called = true;
  ctx.histogram_block_present = true;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 0);
        EXPECT_EQ(s.gauges().size(), 0);
        EXPECT_EQ(s.histograms().size(), 0);
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, EmitCalledWithEmptySets_UnusedMetricsStillPassThrough) {
  counter_c_.used_ = false;
  gauge_b_.used_ = false;

  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1};
  ctx.gauge_buffer_to_snapshot = {0};
  ctx.emit_called = true;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 1);
        EXPECT_EQ(s.counters()[0].counter_.get().name(), "upstream_rq_total");
        EXPECT_EQ(s.gauges().size(), 1);
        EXPECT_EQ(s.gauges()[0].get().name(), "connections_active");
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

TEST_F(ProcessFilterDecisionsTest, EmitCalledNoHistogramBlock_HistogramsPassThrough) {
  StatsFilterContext ctx;
  ctx.counter_buffer_to_snapshot = {0, 1, 2};
  ctx.gauge_buffer_to_snapshot = {0, 1};
  ctx.kept_counter_indices = {0};
  ctx.kept_gauge_indices = {0};
  ctx.emit_called = true;
  ctx.histogram_block_present = false;
  Stats::TagVector global_tags;

  EXPECT_CALL(inner_sink_, flush(testing::_))
      .WillOnce(testing::Invoke([](Stats::MetricSnapshot& s) {
        EXPECT_EQ(s.counters().size(), 1);
        EXPECT_EQ(s.gauges().size(), 1);
        EXPECT_EQ(s.histograms().size(), 2);
      }));

  processFilterDecisionsAndFlush(snapshot_, ctx, global_tags, symbol_table_, inner_sink_);
}

// ====================================================================
// Foreign function: stats_filter_set_global_tags (null pointer)
// ====================================================================

TEST(StatsFilterGlobalTagsTest, NullPointerBadWire) {
  setGlobalTags(nullptr);

  std::string wire;
  appendU32(wire, 1);
  appendStr(wire, "key");

  EXPECT_EQ(callFF("stats_filter_set_global_tags", wire), proxy_wasm::WasmResult::InternalFailure);
}

} // namespace
} // namespace WasmFilter
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
