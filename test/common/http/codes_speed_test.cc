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

class CodeUtilitySpeedTest {
public:
  CodeUtilitySpeedTest()
      : global_store_(symbol_table_), cluster_scope_(symbol_table_), code_stats_(symbol_table_),
        pool_(symbol_table_), prefix_(pool_.add("prefix")) {}

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
    addResponse(200, false, false, pool_.add("test-vhost"), pool_.add("test-cluster"));
    addResponse(200, false, false, empty_stat_name, empty_stat_name, pool_.add("from_az"),
                pool_.add("to_az"));
  }

  void responseTiming() {
    Http::CodeStats::ResponseTimingInfo info{global_store_,
                                             cluster_scope_,
                                             prefix_,
                                             std::chrono::milliseconds(5),
                                             true,
                                             true,
                                             pool_.add("vhost_name"),
                                             pool_.add("req_vcluster_name"),
                                             pool_.add("from_az"),
                                             pool_.add("to_az")};
    code_stats_.chargeResponseTiming(info);
  }

  Stats::SymbolTableImpl symbol_table_;
  Stats::IsolatedStoreImpl global_store_;
  Stats::IsolatedStoreImpl cluster_scope_;
  Http::CodeStatsImpl code_stats_;
  Stats::StatNamePool pool_;
  Stats::StatName prefix_;
};

} // namespace Http
} // namespace Envoy

static void BM_AddResponses(benchmark::State& state) {
  Envoy::Http::CodeUtilitySpeedTest context;

  for (auto _ : state) {
    context.addResponses();
  }
}
BENCHMARK(BM_AddResponses);

static void BM_ResponseTiming(benchmark::State& state) {
  Envoy::Http::CodeUtilitySpeedTest context;

  for (auto _ : state) {
    context.responseTiming();
  }
}
BENCHMARK(BM_ResponseTiming);

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
