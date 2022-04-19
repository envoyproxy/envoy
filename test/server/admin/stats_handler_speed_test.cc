// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_handler.h"

#include "test/mocks/server/admin_stream.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Server {

class StatsHandlerTest {
public:
  StatsHandlerTest() : alloc_(symbol_table_), store_(alloc_) {
    // Benchmark will be 10k clusters each with 100 counters, with 100+
    // character names. The first counter in each scope will be given a value so
    // it will be included in 'usedonly'.
    const std::string prefix(100, 'a');
    for (uint32_t s = 0; s < 10000; ++s) {
      Stats::ScopeSharedPtr scope = store_.createScope(absl::StrCat("scope_", s));
      scopes_.emplace_back(scope);
      for (uint32_t c = 0; c < 100; ++c) {
        Stats::Counter& counter = scope->counterFromString(absl::StrCat(prefix, "_", c));
        if (c == 0) {
          counter.inc();
        }
      }
    }
  }

  /**
   * Issues an admin request against the stats saved in store_.
   */
  uint64_t handlerStats(bool used_only, bool json, const absl::optional<std::regex>& filter,
                        Utility::HistogramBucketsMode histogram_buckets_mode =
                            Utility::HistogramBucketsMode::NoBuckets) {
    Buffer::OwnedImpl data;
    auto response_headers = Http::ResponseHeaderMapImpl::create();
    StatsHandler::handlerStats(store_, used_only, json, filter, histogram_buckets_mode,
                               *response_headers, data);
    return data.length();
  }

  Stats::SymbolTableImpl symbol_table_;
  Stats::AllocatorImpl alloc_;
  Stats::ThreadLocalStoreImpl store_;
  std::vector<Stats::ScopeSharedPtr> scopes_;
};

} // namespace Server
} // namespace Envoy

Envoy::Server::StatsHandlerTest& testContext() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(Envoy::Server::StatsHandlerTest);
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersText(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(false, false, absl::nullopt);
    RELEASE_ASSERT(count > 100 * 1000 * 1000, "expected count > 100M"); // actual = 117,789,000
  }
}
BENCHMARK(BM_AllCountersText)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersText(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(true, false, absl::nullopt);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < 2 * 1000 * 1000, "expected count < 2M"); // actual = 1,168,890
  }
}
BENCHMARK(BM_UsedCountersText)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersText(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  absl::optional<std::regex> filter(std::regex("no-match"));

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(false, false, filter);
    RELEASE_ASSERT(count == 0, "expected count == 0");
  }
}
BENCHMARK(BM_FilteredCountersText)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersJson(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(false, true, absl::nullopt);
    RELEASE_ASSERT(count > 130 * 1000 * 1000, "expected count > 1130M"); // actual = 135,789,011
  }
}
BENCHMARK(BM_AllCountersJson)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersJson(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(true, true, absl::nullopt);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < 2 * 1000 * 1000, "expected count < 2M"); // actual = 1,348,901
  }
}
BENCHMARK(BM_UsedCountersJson)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersJson(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  absl::optional<std::regex> filter(std::regex("no-match"));

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(false, true, filter);
    RELEASE_ASSERT(count < 100, "expected count < 100"); // actual = 12
  }
}
BENCHMARK(BM_FilteredCountersJson)->Unit(benchmark::kMillisecond);
