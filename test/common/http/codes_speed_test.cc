// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/stats/stats.h"

#include "common/common/empty_string.h"
#include "common/http/codes.h"
#include "common/stats/isolated_store_impl.h"

#include "testing/base/public/benchmark.h"

namespace Envoy {
namespace Http {

class CodeUtilitySpeedTest {
public:
  void addResponse(uint64_t code, bool canary, bool internal_request,
                   const std::string& request_vhost_name = EMPTY_STRING,
                   const std::string& request_vcluster_name = EMPTY_STRING,
                   const std::string& from_az = EMPTY_STRING,
                   const std::string& to_az = EMPTY_STRING) {
    Http::CodeUtility::ResponseStatInfo info{
        global_store_,      cluster_scope_,        "prefix.", code,  internal_request,
        request_vhost_name, request_vcluster_name, from_az,   to_az, canary};

    Http::CodeUtility::chargeResponseStat(info);
  }

  void addResponses() {
    addResponse(201, false, false);
    addResponse(301, false, true);
    addResponse(401, false, false);
    addResponse(501, false, true);
    addResponse(200, true, true);
    addResponse(300, false, false);
    addResponse(500, true, false);
    addResponse(200, false, false, "test-vhost", "test-cluster");
    addResponse(200, false, false, "", "", "from_az", "to_az");
  }

  void responseTiming() {
    Http::CodeUtility::ResponseTimingInfo info{
        global_store_, cluster_scope_, "prefix.",    std::chrono::milliseconds(5),
        true,          true,           "vhost_name", "req_vcluster_name",
        "from_az",     "to_az"};
    Http::CodeUtility::chargeResponseTiming(info);
  }

  Stats::IsolatedStoreImpl global_store_;
  Stats::IsolatedStoreImpl cluster_scope_;
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
