// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/stats/stats.h"

#include "common/http/codes.h"
#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {

template <class SymbolTableClass> class CodeUtilitySpeedTest {
public:
  CodeUtilitySpeedTest()
      : global_store_(symbol_table_), cluster_scope_(symbol_table_), code_stats_(symbol_table_),
        pool_(symbol_table_), from_az_(pool_.add("from_az")), prefix_(pool_.add("prefix")),
        req_vcluster_name_(pool_.add("req_vcluster_name")),
        test_cluster_(pool_.add("test-cluster")), test_vhost_(pool_.add("test-vhost")),
        to_az_(pool_.add("to_az")), vhost_name_(pool_.add("vhost_name")) {}

  void addResponse(uint64_t code, bool canary, bool internal_request,
                   Stats::StatName request_vhost_name = Stats::StatName(),
                   Stats::StatName request_vcluster_name = Stats::StatName(),
                   Stats::StatName from_az = Stats::StatName(),
                   Stats::StatName to_az = Stats::StatName()) {
    Http::CodeStats::ResponseStatInfo info{
        global_store_,      cluster_scope_,        prefix_, code,  internal_request,
        request_vhost_name, request_vcluster_name, from_az, to_az, canary};

    code_stats_.chargeResponseStat(info);
  }

  void addResponses() {
    addResponse(201, false, false);
    addResponse(301, false, true);
    addResponse(401, false, false);
    addResponse(501, false, true);
    addResponse(200, true, true);
    addResponse(300, false, false);
    Stats::StatName empty_stat_name;
    addResponse(500, true, false);
    addResponse(200, false, false, test_vhost_, test_cluster_);
    addResponse(200, false, false, empty_stat_name, empty_stat_name, from_az_, to_az_);
  }

  void responseTiming() {
    Http::CodeStats::ResponseTimingInfo info{
        global_store_, cluster_scope_, prefix_,     std::chrono::milliseconds(5),
        true,          true,           vhost_name_, req_vcluster_name_,
        from_az_,      to_az_};
    code_stats_.chargeResponseTiming(info);
  }

  SymbolTableClass symbol_table_;
  Stats::IsolatedStoreImpl global_store_;
  Stats::IsolatedStoreImpl cluster_scope_;
  Http::CodeStatsImpl code_stats_;
  Stats::StatNamePool pool_;
  const Stats::StatName from_az_;
  const Stats::StatName prefix_;
  const Stats::StatName req_vcluster_name_;
  const Stats::StatName test_cluster_;
  const Stats::StatName test_vhost_;
  const Stats::StatName to_az_;
  const Stats::StatName vhost_name_;
};

} // namespace Http
} // namespace Envoy

static void BM_AddResponsesFakeSymtab(benchmark::State& state) {
  Envoy::Http::CodeUtilitySpeedTest<Envoy::Stats::FakeSymbolTableImpl> context;

  for (auto _ : state) {
    context.addResponses();
  }
}
BENCHMARK(BM_AddResponsesFakeSymtab);

static void BM_ResponseTimingFakeSymtab(benchmark::State& state) {
  Envoy::Http::CodeUtilitySpeedTest<Envoy::Stats::FakeSymbolTableImpl> context;

  for (auto _ : state) {
    context.responseTiming();
  }
}
BENCHMARK(BM_ResponseTimingFakeSymtab);

static void BM_AddResponsesRealSymtab(benchmark::State& state) {
  Envoy::Http::CodeUtilitySpeedTest<Envoy::Stats::SymbolTableImpl> context;

  for (auto _ : state) {
    context.addResponses();
  }
}
BENCHMARK(BM_AddResponsesRealSymtab);

static void BM_ResponseTimingRealSymtab(benchmark::State& state) {
  Envoy::Http::CodeUtilitySpeedTest<Envoy::Stats::SymbolTableImpl> context;

  for (auto _ : state) {
    context.responseTiming();
  }
}
BENCHMARK(BM_ResponseTimingRealSymtab);
