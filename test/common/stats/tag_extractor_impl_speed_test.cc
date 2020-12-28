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

static void bmExtractTags(benchmark::State& state) {
  TagProducerImpl tag_extractors{envoy::config::metrics::v3::StatsConfig()};
  const auto idx = state.range(0);
  const auto& p = params[idx];
  absl::string_view str = std::get<0>(p);
  const uint32_t tags_size = std::get<1>(p);

  for (auto _ : state) {
    TagVector tags;
    tag_extractors.produceTags(str, tags);
    RELEASE_ASSERT(tags.size() == tags_size, "");
  }
}
BENCHMARK(bmExtractTags)->DenseRange(0, 26, 1);

} // namespace
} // namespace Stats
} // namespace Envoy
