// Note: this should be run with --compilation_mode=opt
// Running ./bazel-out/k8-opt/bin/test/common/stats/tag_extractor_impl_benchmark
// Run on (24 X 4300 MHz CPU s)
// CPU Caches:
//   L1 Data 32 KiB (x12)
//   L1 Instruction 32 KiB (x12)
//   L2 Unified 1024 KiB (x12)
//   L3 Unified 16896 KiB (x1)
// Load Average: 0.94, 0.75, 0.88
// ***WARNING*** CPU scaling is enabled, the benchmark real time
// measurements may be noisy and will incur extra overhead.
// -----------------------------------------------------------
// Benchmark                 Time             CPU   Iterations
// -----------------------------------------------------------
// BM_ExtractTags/0        1825 ns         1822 ns       387111
// BM_ExtractTags/1         508 ns          507 ns      1000000
// BM_ExtractTags/2         790 ns          789 ns       848523
// BM_ExtractTags/3         644 ns          643 ns      1094132
// BM_ExtractTags/4        1341 ns         1340 ns       531746
// BM_ExtractTags/5         862 ns          861 ns       826768
// BM_ExtractTags/6         338 ns          337 ns      2113684
// BM_ExtractTags/7         588 ns          587 ns      1201340
// BM_ExtractTags/8        1427 ns         1424 ns       491857
// BM_ExtractTags/9        1859 ns         1856 ns       376606
// BM_ExtractTags/10        316 ns          316 ns      2218446
// BM_ExtractTags/11        510 ns          509 ns      1387294
// BM_ExtractTags/12       1136 ns         1134 ns       603194
// BM_ExtractTags/13       1353 ns         1351 ns       526904
// BM_ExtractTags/14       1592 ns         1590 ns       437307
// BM_ExtractTags/15        958 ns          956 ns       716738
// BM_ExtractTags/16        809 ns          808 ns       834782
// BM_ExtractTags/17        806 ns          804 ns       892949
// BM_ExtractTags/18        829 ns          828 ns       867147
// BM_ExtractTags/19        333 ns          333 ns      2070021
// BM_ExtractTags/20        347 ns          347 ns      2071800
// BM_ExtractTags/21        399 ns          398 ns      1769854
// BM_ExtractTags/22        873 ns          872 ns       784136
// BM_ExtractTags/23       2092 ns         2089 ns       336846
// BM_ExtractTags/24        314 ns          314 ns      2221138
// BM_ExtractTags/25        306 ns          306 ns      2317142
// BM_ExtractTags/26        497 ns          497 ns      1417577

#include "envoy/config/metrics/v3/stats.pb.h"

#include "common/common/assert.h"
#include "common/config/well_known_names.h"
#include "common/stats/tag_producer_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Stats {
namespace {

using Params = std::tuple<std::string, uint32_t>;

const std::vector<Params> params = {
    {"listener.127.0.0.1_3012.http.http_prefix.downstream_rq_5xx", 3},
    {"cluster.ratelimit.upstream_rq_timeout", 1},
    {"listener.[__1]_0.ssl.cipher.AES256-SHA", 2},
    {"cluster.ratelimit.ssl.ciphers.ECDHE-RSA-AES128-GCM-SHA256", 2},
    {"listener.[2001_0db8_85a3_0000_0000_8a2e_0370_7334]_3543.ssl.cipher.AES256-SHA", 2},
    {"listener.127.0.0.1_0.ssl.cipher.AES256-SHA", 2},
    {"mongo.mongo_filter.op_reply", 1},
    {"mongo.mongo_filter.cmd.foo_cmd.reply_size", 2},
    {"mongo.mongo_filter.collection.bar_collection.query.multi_get", 2},
    {"mongo.mongo_filter.collection.bar_collection.callsite.baz_callsite.query.scatter_get", 3},
    {"ratelimit.foo_ratelimiter.over_limit", 1},
    {"http.egress_dynamodb_iad.downstream_cx_total", 1},
    {"http.egress_dynamodb_iad.dynamodb.operation.Query.upstream_rq_time", 2},
    {"http.egress_dynamodb_iad.dynamodb.table.bar_table.upstream_rq_time", 2},
    {"http.egress_dynamodb_iad.dynamodb.table.bar_table.capacity.Query.__partition_id=ABC1234", 4},
    {"cluster.grpc_cluster.grpc.grpc_service_1.grpc_method_1.success", 3},
    {"vhost.vhost_1.vcluster.vcluster_1.upstream_rq_2xx", 3},
    {"vhost.vhost_1.vcluster.vcluster_1.upstream_rq_200", 3},
    {"http.egress_dynamodb_iad.user_agent.ios.downstream_cx_total", 2},
    {"auth.clientssl.clientssl_prefix.auth_ip_allowlist", 1},
    {"tcp.tcp_prefix.downstream_flow_control_resumed_reading_total", 1},
    {"udp.udp_prefix-with-dashes.downstream_flow_control_resumed_reading_total", 1},
    {"http.fault_connection_manager.fault.fault_cluster.aborts_injected", 2},
    {"http.rds_connection_manager.rds.route_config.123.update_success", 2},
    {"listener_manager.worker_123.dispatcher.loop_duration_us", 1},
    {"mongo_mongo_mongo_mongo.this_is_rather_long_string_which "
     "does_not_match_and_consumes_a_lot_in_case_of_backtracking_imposed_by_greedy_pattern",
     0},
    {"another_long_but_matching_string_which_may_consume_resources_if_missing_end_of_line_lock_rq_"
     "2xx",
     1},
};

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_ExtractTags(benchmark::State& state) {
  TagProducerImpl tag_extractors{envoy::config::metrics::v3::StatsConfig()};
  const auto idx = state.range(0);
  const auto& p = params[idx];
  absl::string_view str = std::get<0>(p);
  const uint32_t tags_size = std::get<1>(p);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    TagVector tags;
    tag_extractors.produceTags(str, tags);
    RELEASE_ASSERT(tags.size() == tags_size, "");
  }
}
BENCHMARK(BM_ExtractTags)->DenseRange(0, 26, 1);

} // namespace
} // namespace Stats
} // namespace Envoy
