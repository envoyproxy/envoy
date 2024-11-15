#include "test/common/stats/stat_test_utility.h"

#include "source/common/common/assert.h"
#include "source/common/memory/stats.h"

namespace Envoy {
namespace Stats {

bool operator==(const ParentHistogram::Bucket& a, const ParentHistogram::Bucket& b) {
  return a.count_ == b.count_ && std::abs(a.lower_bound_ - b.lower_bound_) < 0.001 &&
         std::abs(a.width_ - b.width_) < 0.001;
}

std::ostream& operator<<(std::ostream& out, const ParentHistogram::Bucket& bucket) {
  return out << "(min_value=" << bucket.lower_bound_ << ", width=" << bucket.width_
             << ", count=" << bucket.count_ << ")";
}

namespace TestUtil {

void forEachSampleStat(int num_clusters, bool include_other_stats,
                       std::function<void(absl::string_view)> fn) {
  // These are stats that are repeated for each cluster as of Oct 2018, with a
  // very basic configuration with no traffic.
  static const char* cluster_stats[] = {"bind_errors",
                                        "lb_healthy_panic",
                                        "lb_local_cluster_not_ok",
                                        "lb_recalculate_zone_structures",
                                        "lb_subsets_active",
                                        "lb_subsets_created",
                                        "lb_subsets_fallback",
                                        "lb_subsets_removed",
                                        "lb_subsets_selected",
                                        "lb_zone_cluster_too_small",
                                        "lb_zone_no_capacity_left",
                                        "lb_zone_number_differs",
                                        "lb_zone_routing_all_directly",
                                        "lb_zone_routing_cross_zone",
                                        "lb_zone_routing_sampled",
                                        "max_host_weight",
                                        "membership_change",
                                        "membership_healthy",
                                        "membership_total",
                                        "original_dst_host_invalid",
                                        "retry_or_shadow_abandoned",
                                        "update_attempt",
                                        "update_empty",
                                        "update_failure",
                                        "update_no_rebuild",
                                        "update_success",
                                        "upstream_cx_active",
                                        "upstream_cx_close_notify",
                                        "upstream_cx_connect_attempts_exceeded",
                                        "upstream_cx_connect_fail",
                                        "upstream_cx_connect_timeout",
                                        "upstream_cx_destroy",
                                        "upstream_cx_destroy_local",
                                        "upstream_cx_destroy_local_with_active_rq",
                                        "upstream_cx_destroy_remote",
                                        "upstream_cx_destroy_remote_with_active_rq",
                                        "upstream_cx_destroy_with_active_rq",
                                        "upstream_cx_http1_total",
                                        "upstream_cx_http2_total",
                                        "upstream_cx_idle_timeout",
                                        "upstream_cx_max_requests",
                                        "upstream_cx_none_healthy",
                                        "upstream_cx_overflow",
                                        "upstream_cx_protocol_error",
                                        "upstream_cx_rx_bytes_buffered",
                                        "upstream_cx_rx_bytes_total",
                                        "upstream_cx_total",
                                        "upstream_cx_tx_bytes_buffered",
                                        "upstream_cx_tx_bytes_total",
                                        "upstream_flow_control_backed_up_total",
                                        "upstream_flow_control_drained_total",
                                        "upstream_flow_control_paused_reading_total",
                                        "upstream_flow_control_resumed_reading_total",
                                        "upstream_rq_active",
                                        "upstream_rq_cancelled",
                                        "upstream_rq_completed",
                                        "upstream_rq_maintenance_mode",
                                        "upstream_rq_pending_active",
                                        "upstream_rq_pending_failure_eject",
                                        "upstream_rq_pending_overflow",
                                        "upstream_rq_pending_total",
                                        "upstream_rq_per_try_timeout",
                                        "upstream_rq_retry",
                                        "upstream_rq_retry_overflow",
                                        "upstream_rq_retry_success",
                                        "upstream_rq_rx_reset",
                                        "upstream_rq_timeout",
                                        "upstream_rq_total",
                                        "upstream_rq_tx_reset",
                                        "version"};

  // These are the other stats that appear in the admin /stats request when made
  // prior to any requests.
  static const char* other_stats[] = {"http.admin.downstream_cx_length_ms",
                                      "http.admin.downstream_rq_time",
                                      "http.ingress_http.downstream_cx_length_ms",
                                      "http.ingress_http.downstream_rq_time",
                                      "listener.0.0.0.0_40000.downstream_cx_length_ms",
                                      "listener.admin.downstream_cx_length_ms"};

  for (int cluster = 0; cluster <= num_clusters; ++cluster) {
    for (const auto& cluster_stat : cluster_stats) {
      fn(absl::StrCat("cluster.service_", cluster, ".", cluster_stat));
    }
  }
  if (include_other_stats) {
    for (const auto& other_stat : other_stats) {
      fn(other_stat);
    }
  }
}

TestScope::TestScope(const std::string& prefix, TestStore& store)
    : IsolatedScopeImpl(prefix, store), store_(store), prefix_str_(addDot(prefix)) {}
TestScope::TestScope(StatName prefix, TestStore& store)
    : IsolatedScopeImpl(prefix, store), store_(store),
      prefix_str_(addDot(store.symbolTable().toString(prefix))) {}

// Override the Stats::Store methods for name-based lookup of stats, to use
// and update the string-maps in this class. Note that IsolatedStoreImpl
// does not support deletion of stats, so we only have to track additions
// to keep the maps up-to-date.
//
// Stats::Scope
Counter& TestScope::counterFromString(const std::string& leaf_name) {
  std::string name = prefix_str_ + leaf_name;
  Counter*& counter_ref = store_.counter_map_[name];
  if (counter_ref == nullptr) {
    counter_ref = &IsolatedScopeImpl::counterFromString(leaf_name);
  }
  return *counter_ref;
}

Gauge& TestScope::gaugeFromString(const std::string& leaf_name, Gauge::ImportMode import_mode) {
  std::string name = prefix_str_ + leaf_name;
  Gauge*& gauge_ref = store_.gauge_map_[name];
  if (gauge_ref == nullptr) {
    gauge_ref = &IsolatedScopeImpl::gaugeFromString(leaf_name, import_mode);
  }
  return *gauge_ref;
}

Histogram& TestScope::histogramFromString(const std::string& leaf_name, Histogram::Unit unit) {
  std::string name = prefix_str_ + leaf_name;
  Histogram*& histogram_ref = store_.histogram_map_[name];
  if (histogram_ref == nullptr) {
    histogram_ref = &IsolatedScopeImpl::histogramFromString(leaf_name, unit);
  }
  return *histogram_ref;
}

std::string TestScope::statNameWithTags(const StatName& stat_name,
                                        StatNameTagVectorOptConstRef tags) {
  TagUtility::TagStatNameJoiner joiner(prefix(), stat_name, tags, symbolTable());
  return symbolTable().toString(joiner.nameWithTags());
}

void TestScope::verifyConsistency(StatName ref_stat_name, StatName stat_name,
                                  StatNameTagVectorOptConstRef tags) {
  // Ensures StatNames with the same string representation are specified
  // consistently using symbolic/dynamic components on every access.
  TagUtility::TagStatNameJoiner joiner(prefix(), stat_name, tags, symbolTable());
  StatName joined_stat_name = joiner.nameWithTags();
  ASSERT(ref_stat_name == joined_stat_name,
         absl::StrCat("Inconsistent dynamic vs symbolic stat name specification: ref_stat_name=",
                      symbolTable().toString(ref_stat_name),
                      " stat_name=", symbolTable().toString(joined_stat_name)));
}

Counter& TestScope::counterFromStatNameWithTags(const StatName& stat_name,
                                                StatNameTagVectorOptConstRef tags) {
  std::string name = statNameWithTags(stat_name, tags);
  Counter*& counter_ref = store_.counter_map_[name];
  if (counter_ref == nullptr) {
    counter_ref = &IsolatedScopeImpl::counterFromStatNameWithTags(stat_name, tags);
  } else {
    verifyConsistency(counter_ref->statName(), stat_name, tags);
  }
  return *counter_ref;
}

Gauge& TestScope::gaugeFromStatNameWithTags(const StatName& stat_name,
                                            StatNameTagVectorOptConstRef tags,
                                            Gauge::ImportMode import_mode) {
  std::string name = statNameWithTags(stat_name, tags);
  Gauge*& gauge_ref = store_.gauge_map_[name];
  if (gauge_ref == nullptr) {
    gauge_ref = &IsolatedScopeImpl::gaugeFromStatNameWithTags(stat_name, tags, import_mode);
  } else {
    verifyConsistency(gauge_ref->statName(), stat_name, tags);
  }
  return *gauge_ref;
}

Histogram& TestScope::histogramFromStatNameWithTags(const StatName& stat_name,
                                                    StatNameTagVectorOptConstRef tags,
                                                    Histogram::Unit unit) {
  std::string name = statNameWithTags(stat_name, tags);
  Histogram*& histogram_ref = store_.histogram_map_[name];
  if (histogram_ref == nullptr) {
    histogram_ref = &IsolatedScopeImpl::histogramFromStatNameWithTags(stat_name, tags, unit);
  } else {
    verifyConsistency(histogram_ref->statName(), stat_name, tags);
  }
  return *histogram_ref;
}

ScopeSharedPtr TestStore::makeScope(StatName name) {
  return std::make_shared<TestScope>(name, *this);
}

TestStore::TestStore() : IsolatedStoreImpl(*global_symbol_table_) {}

TestStore::TestStore(SymbolTable& symbol_table) : IsolatedStoreImpl(symbol_table) {}

template <class StatType>
using StatTypeOptConstRef = absl::optional<std::reference_wrapper<const StatType>>;

template <class StatType>
static StatTypeOptConstRef<StatType>
findByString(const std::string& name, const absl::flat_hash_map<std::string, StatType*>& map) {
  StatTypeOptConstRef<StatType> ret;
  auto iter = map.find(name);
  if (iter != map.end()) {
    ret = *iter->second;
  }
  return ret;
}

CounterOptConstRef TestStore::findCounterByString(const std::string& name) const {
  return findByString<Counter>(name, counter_map_);
}

GaugeOptConstRef TestStore::findGaugeByString(const std::string& name) const {
  return findByString<Gauge>(name, gauge_map_);
}

HistogramOptConstRef TestStore::findHistogramByString(const std::string& name) const {
  return findByString<Histogram>(name, histogram_map_);
}

std::vector<uint64_t> TestStore::histogramValues(const std::string& name, bool clear) {
  auto it = histogram_values_map_.find(name);
  ASSERT(it != histogram_values_map_.end(), absl::StrCat("Couldn't find histogram ", name));
  std::vector<uint64_t> copy = it->second;
  if (clear) {
    it->second.clear();
  }
  return copy;
}

bool TestStore::histogramRecordedValues(const std::string& name) const {
  return histogram_values_map_.contains(name);
}

// TODO(jmarantz): this utility is intended to be used both for unit tests
// and fuzz tests. But those have different checking macros, e.g. EXPECT_EQ vs
// FUZZ_ASSERT.
std::vector<uint8_t> serializeDeserializeNumber(uint64_t number) {
  uint64_t num_bytes = SymbolTableImpl::Encoding::encodingSizeBytes(number);
  const uint64_t block_size = 10;
  MemBlockBuilder<uint8_t> mem_block(block_size);
  SymbolTableImpl::Encoding::appendEncoding(number, mem_block);
  num_bytes += mem_block.capacityRemaining();
  RELEASE_ASSERT(block_size == num_bytes, absl::StrCat("Encoding size issue: block_size=",
                                                       block_size, " num_bytes=", num_bytes));
  absl::Span<uint8_t> span = mem_block.span();
  RELEASE_ASSERT(number == SymbolTableImpl::Encoding::decodeNumber(span.data()).first, "");
  return {span.data(), span.data() + span.size()};
}

void serializeDeserializeString(absl::string_view in) {
  MemBlockBuilder<uint8_t> mem_block(SymbolTableImpl::Encoding::totalSizeBytes(in.size()));
  SymbolTableImpl::Encoding::appendEncoding(in.size(), mem_block);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(in.data());
  mem_block.appendData(absl::MakeSpan(data, data + in.size()));
  RELEASE_ASSERT(mem_block.capacityRemaining() == 0, "");
  absl::Span<uint8_t> span = mem_block.span();
  const std::pair<uint64_t, uint64_t> number_consumed =
      SymbolTableImpl::Encoding::decodeNumber(span.data());
  RELEASE_ASSERT(number_consumed.first == in.size(), absl::StrCat("size matches: ", in));
  span.remove_prefix(number_consumed.second);
  const absl::string_view out(reinterpret_cast<const char*>(span.data()), span.size());
  RELEASE_ASSERT(in == out, absl::StrCat("'", in, "' != '", out, "'"));
}

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
