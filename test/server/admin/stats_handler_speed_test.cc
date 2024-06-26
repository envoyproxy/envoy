// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_handler.h"

#include "test/benchmark/main.h"
#include "test/common/stats/real_thread_test_base.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Server {

class FastMockCluster;

class FastMockClusterManager : public testing::StrictMock<Upstream::MockClusterManager> {
public:
  ClusterInfoMaps clusters() const override { return clusters_; }

  ClusterInfoMaps clusters_;
  std::vector<std::unique_ptr<FastMockCluster>> clusters_storage_;
  Stats::TestUtil::TestStore store_;
  bool per_endpoint_enabled_{false};
};

// Override the methods used by this test so that using a mock doesn't affect performance.
class FastMockCluster : public testing::StrictMock<Upstream::MockCluster>,
                        public testing::StrictMock<Upstream::MockPrioritySet> {
public:
  FastMockCluster(const std::string& name, FastMockClusterManager& cm)
      : cm_(cm), scope_(name, cm_.store_), name_(name) {}

  Upstream::ClusterInfoConstSharedPtr info() const override { return info_; }
  const Upstream::PrioritySet& prioritySet() const override { return *this; }
  PrioritySet& prioritySet() override { return *this; }
  const std::vector<Upstream::HostSetPtr>& hostSetsPerPriority() const override {
    return host_sets_;
  }

  class ClusterInfo : public testing::StrictMock<Upstream::MockClusterInfo> {
  public:
    ClusterInfo(FastMockCluster& parent) : parent_(parent) {}

    bool perEndpointStatsEnabled() const override { return parent_.cm_.per_endpoint_enabled_; }
    const std::string& observabilityName() const override { return parent_.name_; }
    Stats::Scope& statsScope() const override { return parent_.scope_; }

    FastMockCluster& parent_;
  };

  FastMockClusterManager& cm_;
  Stats::TestUtil::TestScope scope_;
  std::shared_ptr<ClusterInfo> info_{std::make_shared<ClusterInfo>(*this)};
  std::string name_;
  std::vector<Upstream::HostSetPtr> host_sets_;
};

class FastMockHostSet : public testing::StrictMock<Upstream::MockHostSet> {
public:
  const Upstream::HostVector& hosts() const override { return hosts_; }

  Upstream::HostVector hosts_;
};

class FastMockHost : public testing::StrictMock<Upstream::MockHostLight> {
public:
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::string& hostname() const override { return hostname_; }

  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>
  counters() const override {
    return host_stats_.counters();
  }

  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>
  gauges() const override {
    return host_stats_.gauges();
  }

  Health coarseHealth() const override { return Upstream::Host::Health::Healthy; }

  Network::UpstreamTransportSocketFactory&
  resolveTransportSocketFactory(const Network::Address::InstanceConstSharedPtr&,
                                const envoy::config::core::v3::Metadata*) const override {
    IS_ENVOY_BUG("unexpected call to resolveTransportSocketFactory");
    Network::UpstreamTransportSocketFactory* ptr = nullptr;
    return *ptr;
  }

  Network::Address::InstanceConstSharedPtr address_;
  std::string hostname_;
  mutable Upstream::HostStats host_stats_;
};

class StatsHandlerTest : public Stats::ThreadLocalRealThreadsMixin {
public:
  StatsHandlerTest() : ThreadLocalRealThreadsMixin(1) {
    runOnMainBlocking([this]() { init(); });
    Envoy::benchmark::setCleanupHook([this]() { delete this; });
  }

  static constexpr uint32_t NumClusters = 10000;

  void init() {
    // Benchmark will be 10k clusters each with 100 counters, with 100+
    // character names. The first counter in each scope will be given a value so
    // it will be included in 'usedonly'.
    const std::string prefix(100, 'a');
    for (uint32_t s = 0; s < NumClusters; ++s) {
      Stats::ScopeSharedPtr scope = store_->createScope(absl::StrCat("scope_", s));
      scopes_.emplace_back(scope);
      for (uint32_t c = 0; c < 100; ++c) {
        Stats::Counter& counter = scope->counterFromString(absl::StrCat(prefix, "_", c));
        if (c == 0) {
          counter.inc();
        }
      }
    }

    for (uint32_t s = 0; s < 100; ++s) {
      Stats::Histogram& h =
          store_->histogramFromString(absl::StrCat("h", s), Stats::Histogram::Unit::Unspecified);

      // Populate a dense histogram. This data is copied from
      // test/integration/admin_html/histogram_test.js and reflects lower-bounds
      // seen in production, albeit without the 'count' or 'width' data present in
      // the test and needed for accurate visualization. The purpose of this is to
      // get a realistic view of how expensive it is to serialize each histogram as
      // json.
      // clang-format off
      constexpr double vals[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
        24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
        44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
        64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
        84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 110.00000000000001,
        120, 130, 140, 150, 160, 170, 180, 190, 200, 210, 220.00000000000003, 229.99999999999997,
        240, 250, 260, 270, 280, 290, 300, 310, 320, 330, 340, 350, 360, 370, 380, 390,
        400, 409.99999999999994, 420, 430, 440.00000000000006, 450, 459.99999999999994,
        470, 480, 490.00000000000006, 500, 509.99999999999994, 520, 530, 540, 550, 560,
        570, 580, 590, 600, 610, 620, 630, 640, 650, 660, 670, 680, 690, 700, 710, 720,
        730, 740, 750, 760, 770, 780, 790, 800, 810, 819.99999999999989, 830.00000000000011,
        840, 850, 860, 869.99999999999989, 880.00000000000011, 890, 900, 910, 919.99999999999989,
        930.00000000000011, 940, 950, 960, 969.99999999999989, 980.00000000000011, 990,
        1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200,
        2300, 2400, 2500, 2600, 2700, 2900, 3000, 3100, 3200, 3300, 3400, 3500, 3600,
        3800, 3900, 4000, 4300, 4400, 4700, 5300, 12000, 15000};
      // clang-format on

      for (uint64_t val : vals) {
        h.recordValue(val);
      }
    }
    store_->mergeHistograms([]() {});
  }

  void initClusterInfo() {
    ENVOY_LOG_MISC(error, "Initializing cluster info; slow to construct and destruct...");
    endpoint_stats_initialized_ = true;

    cm_.store_.fixed_tags_ = Stats::TagVector{{"fixed-tag", "fixed-value"}};
    for (uint32_t i = 0; i < NumClusters; i++) {
      std::string name = absl::StrCat("cluster_", i);
      cm_.clusters_storage_.push_back(std::make_unique<FastMockCluster>(name, cm_));
      FastMockCluster& cluster = *cm_.clusters_storage_.back();
      auto host_set = std::make_unique<FastMockHostSet>();
      for (uint32_t host_num = 0; host_num < 10; host_num++) {
        auto host = std::make_unique<FastMockHost>();
        host->hostname_ = fmt::format("host{}.example.com", host_num);
        host->address_ = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", host_num + 1);

        host_set->hosts_.push_back(std::move(host));
      }

      cluster.host_sets_.push_back(std::move(host_set));
      cm_.clusters_.active_clusters_.emplace(name, cluster);
    }
  }

  void setPerEndpointStats(bool enabled) {
    if (enabled && !endpoint_stats_initialized_) {
      initClusterInfo();
    }
    cm_.per_endpoint_enabled_ = enabled;
  }

  /**
   * Issues an admin request against the stats saved in store_.
   */
  uint64_t handlerStats(const StatsParams& params) {
    Buffer::OwnedImpl data;
    if (params.format_ == StatsFormat::Prometheus) {
      StatsHandler::prometheusRender(*store_, custom_namespaces_, cm_, params, data);
      return data.length();
    }
    Admin::RequestPtr request = StatsHandler::makeRequest(*store_, params, cm_);
    auto response_headers = Http::ResponseHeaderMapImpl::create();
    request->start(*response_headers);
    uint64_t count = 0;
    bool more = true;
    do {
      more = request->nextChunk(data);
      count += data.length();
      data.drain(data.length());
    } while (more);
    return count;
  }

  std::vector<Stats::ScopeSharedPtr> scopes_;
  Envoy::Stats::CustomStatNamespacesImpl custom_namespaces_;
  FastMockClusterManager cm_;
  bool endpoint_stats_initialized_{false};
};

} // namespace Server
} // namespace Envoy

static Envoy::Server::StatsHandlerTest& get() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(Envoy::Server::StatsHandlerTest);
}

Envoy::Server::StatsHandlerTest& testContext(bool per_endpoint_enabled) {
  Envoy::Event::Libevent::Global::initialize();
  auto& context = get();
  context.setPerEndpointStats(per_endpoint_enabled);
  return context;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersText(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  params.type_ = Envoy::Server::StatsType::Counters;

  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 100 * 1000 * 1000, "expected count > 100M"); // actual = 117,789,000
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
// BENCHMARK(BM_AllCountersText)->Range(false, true)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_AllCountersText, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_AllCountersText, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersText(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?usedonly&type=Counters", response);

  const uint64_t upper_limit = per_endpoint_stats ? 50 * 1000 * 1000 : 2 * 1000 * 1000;
  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < upper_limit, "expected count < upper_limit");
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_UsedCountersText, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UsedCountersText, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersText(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?filter=no-match&type=Counters", response);

  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count == 0, "expected count == 0");
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_FilteredCountersText, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_FilteredCountersText, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersJson(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=json&type=Counters", response);

  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 130 * 1000 * 1000, "expected count > 130M"); // actual = 135,789,011
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_AllCountersJson, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_AllCountersJson, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersJson(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=json&usedonly&type=Counters", response);

  const uint64_t upper_limit = per_endpoint_stats ? 60 * 1000 * 1000 : 2 * 1000 * 1000;
  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < upper_limit, "expected count < upper_limit");
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_UsedCountersJson, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UsedCountersJson, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersJson(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=json&filter=no-match&type=Counters", response);

  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count < 100, "expected count < 100"); // actual = 12
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_FilteredCountersJson, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_FilteredCountersJson, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersPrometheus(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=prometheus&type=Counters", response);

  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 250 * 1000 * 1000, "expected count > 250M"); // actual = 261,578,000
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_AllCountersPrometheus, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_AllCountersPrometheus, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersPrometheus(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=prometheus&usedonly&type=Counters", response);

  const uint64_t upper_limit = per_endpoint_stats ? 200 * 1000 * 1000 : 3 * 1000 * 1000;
  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < upper_limit, "expected count < upper_limit");
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_UsedCountersPrometheus, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UsedCountersPrometheus, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersPrometheus(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=prometheus&filter=no-match&type=Counters", response);

  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count == 0, "expected count == 0");
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_FilteredCountersPrometheus, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_FilteredCountersPrometheus, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_HistogramsJson(benchmark::State& state, bool per_endpoint_stats) {
  Envoy::Server::StatsHandlerTest& test_context = testContext(per_endpoint_stats);
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=json&type=Histograms&histogram_buckets=detailed", response);

  uint64_t count;
  for (auto _ : state) { // NOLINT
    count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 1750000 && count < 2000000,
                   absl::StrCat("count=", count, ", expected > 1.7M"));
  }

  auto label = absl::StrCat("output per iteration: ", count);
  state.SetLabel(label);
}
BENCHMARK_CAPTURE(BM_HistogramsJson, per_endpoint_stats_disabled, false)
    ->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_HistogramsJson, per_endpoint_stats_enabled, true)
    ->Unit(benchmark::kMillisecond);
