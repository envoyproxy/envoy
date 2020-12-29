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
// ------------------------------------------------------------
// Benchmark                  Time             CPU   Iterations
// ------------------------------------------------------------
// BM_ExtractTags/0        1796 ns         1793 ns       401011
// BM_ExtractTags/1         513 ns          512 ns      1364401
// BM_ExtractTags/2         794 ns          793 ns       899533
// BM_ExtractTags/3         646 ns          645 ns      1091666
// BM_ExtractTags/4        1315 ns         1314 ns       523727
// BM_ExtractTags/5         854 ns          853 ns       830341
// BM_ExtractTags/6         336 ns          336 ns      2085144
// BM_ExtractTags/7         574 ns          573 ns      1205468
// BM_ExtractTags/8        1264 ns         1262 ns       542151
// BM_ExtractTags/9        1724 ns         1721 ns       408224
// BM_ExtractTags/10        316 ns          316 ns      2203384
// BM_ExtractTags/11        522 ns          521 ns      1383972
// BM_ExtractTags/12       1159 ns         1157 ns       591665
// BM_ExtractTags/13       1348 ns         1346 ns       518341
// BM_ExtractTags/14       1605 ns         1603 ns       445517
// BM_ExtractTags/15        959 ns          958 ns       720045
// BM_ExtractTags/16        820 ns          818 ns       857287
// BM_ExtractTags/17        806 ns          805 ns       833224
// BM_ExtractTags/18        843 ns          842 ns       830836
// BM_ExtractTags/19        343 ns          343 ns      2088403
// BM_ExtractTags/20        351 ns          350 ns      1988046
// BM_ExtractTags/21        404 ns          404 ns      1711767
// BM_ExtractTags/22        874 ns          873 ns       802379
// BM_ExtractTags/23       2041 ns         2038 ns       334105
// BM_ExtractTags/24        316 ns          316 ns      2216693
// BM_ExtractTags/25        298 ns          298 ns      2302454
// BM_ExtractTags/26        506 ns          506 ns      1384901

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
