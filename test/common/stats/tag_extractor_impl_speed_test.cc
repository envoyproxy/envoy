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
// BM_ExtractTags/0        1759 ns         1757 ns       397721
// BM_ExtractTags/1         498 ns          497 ns      1386765
// BM_ExtractTags/2         814 ns          813 ns       789388
// BM_ExtractTags/3         621 ns          620 ns      1109055
// BM_ExtractTags/4        1320 ns         1318 ns       536701
// BM_ExtractTags/5         882 ns          880 ns       817115
// BM_ExtractTags/6         327 ns          327 ns      2171259
// BM_ExtractTags/7         572 ns          571 ns      1205250
// BM_ExtractTags/8        1238 ns         1236 ns       558481
// BM_ExtractTags/9        1669 ns         1667 ns       414483
// BM_ExtractTags/10        310 ns          310 ns      2237065
// BM_ExtractTags/11        476 ns          476 ns      1465925
// BM_ExtractTags/12       1102 ns         1100 ns       631707
// BM_ExtractTags/13       1307 ns         1305 ns       513760
// BM_ExtractTags/14       1583 ns         1581 ns       447159
// BM_ExtractTags/15        957 ns          956 ns       729726
// BM_ExtractTags/16        822 ns          821 ns       869110
// BM_ExtractTags/17        821 ns          820 ns       839293
// BM_ExtractTags/18        783 ns          782 ns       898442
// BM_ExtractTags/19        330 ns          329 ns      2098821
// BM_ExtractTags/20        342 ns          342 ns      2044062
// BM_ExtractTags/21        389 ns          389 ns      1785110
// BM_ExtractTags/22        847 ns          846 ns       831652
// BM_ExtractTags/23       2022 ns         2019 ns       353368
// BM_ExtractTags/24        306 ns          305 ns      2226702
// BM_ExtractTags/25        277 ns          277 ns      2516796
// BM_ExtractTags/26        494 ns          494 ns      1363306

#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/well_known_names.h"
#include "source/common/stats/tag_producer_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Stats {
namespace {

using Params = std::tuple<std::string, uint32_t>;

const std::vector<Params> params = {
    {"listener.127.0.0.1_3012.http.http_prefix.downstream_rq_5xx", 3},
    {"cluster.ratelimit.upstream_rq_timeout", 1},
    {"listener.[__1]_0.ssl.ciphers.AES256-SHA", 2},
    {"cluster.ratelimit.ssl.ciphers.ECDHE-RSA-AES128-GCM-SHA256", 2},
    {"listener.[2001_0db8_85a3_0000_0000_8a2e_0370_7334]_3543.ssl.ciphers.AES256-SHA", 2},
    {"listener.127.0.0.1_0.ssl.ciphers.AES256-SHA", 2},
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
  const Stats::TagVector tags;
  auto tag_extractors =
      TagProducerImpl::createTagProducer(envoy::config::metrics::v3::StatsConfig(), tags).value();
  const auto idx = state.range(0);
  const auto& p = params[idx];
  absl::string_view str = std::get<0>(p);
  const uint32_t tags_size = std::get<1>(p);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    TagVector tags;
    tag_extractors->produceTags(str, tags);
    RELEASE_ASSERT(tags.size() == tags_size,
                   absl::StrCat("tags.size()=", tags.size(), " tags_size==", tags_size));
  }
}
BENCHMARK(BM_ExtractTags)->DenseRange(0, 26, 1);

} // namespace
} // namespace Stats
} // namespace Envoy
