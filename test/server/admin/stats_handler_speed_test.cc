// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_handler.h"

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
  uint64_t handlerStats(const StatsParams& params) {
    Buffer::OwnedImpl data;
    if (params.format_ == Envoy::Server::StatsFormat::Prometheus) {
      Envoy::Server::StatsHandler::prometheusRender(store_, custom_namespaces_, params, data);
      return data.length();
    }
    Admin::RequestPtr request = StatsHandler::makeRequest(store_, params);
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

  Stats::SymbolTableImpl symbol_table_;
  Stats::AllocatorImpl alloc_;
  Stats::ThreadLocalStoreImpl store_;
  std::vector<Stats::ScopeSharedPtr> scopes_;
  Envoy::Stats::CustomStatNamespacesImpl custom_namespaces_;
};

} // namespace Server
} // namespace Envoy

Envoy::Server::StatsHandlerTest& testContext() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(Envoy::Server::StatsHandlerTest);
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersText(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 100 * 1000 * 1000, "expected count > 100M"); // actual = 117,789,000
  }
}
BENCHMARK(BM_AllCountersText)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersText(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?usedonly", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < 2 * 1000 * 1000, "expected count < 2M"); // actual = 1,168,890
  }
}
BENCHMARK(BM_UsedCountersText)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersText(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?filter=no-match", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count == 0, "expected count == 0");
  }
}
BENCHMARK(BM_FilteredCountersText)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersJson(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=json", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 130 * 1000 * 1000, "expected count > 130M"); // actual = 135,789,011
  }
}
BENCHMARK(BM_AllCountersJson)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersJson(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=json&usedonly", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < 2 * 1000 * 1000, "expected count < 2M"); // actual = 1,348,901
  }
}
BENCHMARK(BM_UsedCountersJson)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersJson(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=json&filter=no-match", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count < 100, "expected count < 100"); // actual = 12
  }
}
BENCHMARK(BM_FilteredCountersJson)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AllCountersPrometheus(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=prometheus", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 250 * 1000 * 1000, "expected count > 250M"); // actual = 261,578,000
  }
}
BENCHMARK(BM_AllCountersPrometheus)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_UsedCountersPrometheus(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=prometheus&usedonly", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count > 1000 * 1000, "expected count > 1M");
    RELEASE_ASSERT(count < 3 * 1000 * 1000, "expected count < 3M"); // actual = 2,597,780
  }
}
BENCHMARK(BM_UsedCountersPrometheus)->Unit(benchmark::kMillisecond);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FilteredCountersPrometheus(benchmark::State& state) {
  Envoy::Server::StatsHandlerTest& test_context = testContext();
  Envoy::Server::StatsParams params;
  Envoy::Buffer::OwnedImpl response;
  params.parse("?format=prometheus&filter=no-match", response);

  for (auto _ : state) { // NOLINT
    uint64_t count = test_context.handlerStats(params);
    RELEASE_ASSERT(count == 0, "expected count == 0");
  }
}
BENCHMARK(BM_FilteredCountersPrometheus)->Unit(benchmark::kMillisecond);
