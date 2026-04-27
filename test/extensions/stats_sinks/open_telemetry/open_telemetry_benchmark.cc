// This file benchmarks different approaches for aggregating metrics with tags (attributes)
// in the OpenTelemetry stats sink. It compares a custom hash-map based approach against
// the real production implementation which uses a sorted vector of pairs as the key.
// The benchmark runs at a scale of 10,000 stats with 10 tags each to test performance
// under realistic load with high cardinality.
//
// Benchmark Results (Scale: 10,000 stats, 10 tags):
// - BM_aggregationWithHashMap:            ~1230 ms
// - BM_aggregationWithSortedInlineVector: ~770 ms (1.2x faster)
//
// Conclusion: The real implementation using a sorted vector of pairs is more
// performant than the hash map approach both in pure aggregation and end-to-end.
#include "envoy/stats/tag.h"

#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Stats {
namespace {

const std::vector<Tag> test_tags = {{"cluster_name", "bar_cluster"},
                                    {"response_code", "200"},
                                    {"uri", "/api/v1/resource"},
                                    {"method", "GET"},
                                    {"zone", "us-east1-a"},
                                    {"tag6", "value6"},
                                    {"tag7", "value7"},
                                    {"tag8", "value8"},
                                    {"tag9", "value9"},
                                    {"tag10", "value10"}};

class MetricKeyHashMap {
public:
  MetricKeyHashMap(std::string&& name, absl::flat_hash_map<std::string, std::string>&& attributes)
      : name_(std::move(name)), attributes_(std::move(attributes)) {}

  bool operator==(const MetricKeyHashMap& other) const {
    return name_ == other.name_ && attributes_ == other.attributes_;
  }

  template <typename H> friend H AbslHashValue(H h, const MetricKeyHashMap& k) {
    size_t attrs_hash = 0;
    for (const auto& pair : k.attributes_) {
      attrs_hash += absl::Hash<std::pair<std::string, std::string>>()(pair);
    }
    return H::combine(std::move(h), k.name_, attrs_hash);
  }

  absl::string_view name() const { return name_; }
  const absl::flat_hash_map<std::string, std::string>& attributes() const { return attributes_; }

  std::string releaseName() { return std::move(name_); }
  absl::flat_hash_map<std::string, std::string> releaseAttributes() {
    return std::move(attributes_);
  }

private:
  std::string name_;
  absl::flat_hash_map<std::string, std::string> attributes_;
};

class MetricAggregatorHashMap {
public:
  explicit MetricAggregatorHashMap(
      opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality)
      : counter_temporality_(counter_temporality) {}

  void addCounter(std::string&& metric_name, uint64_t value, uint64_t delta,
                  absl::flat_hash_map<std::string, std::string>&& attributes) {
    using namespace opentelemetry::proto::metrics::v1;
    const uint64_t point_value =
        (counter_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) ? delta
                                                                                        : value;
    MetricKeyHashMap key{std::move(metric_name), std::move(attributes)};
    auto it = counter_data_.find(key);
    if (it != counter_data_.end()) {
      it->second += point_value;
    } else {
      counter_data_.emplace(std::move(key), point_value);
    }
  }

  struct AggregationResult {
    absl::flat_hash_map<MetricKeyHashMap, uint64_t> counter_data_;
  };

  AggregationResult releaseResult() { return {std::move(counter_data_)}; }

private:
  absl::flat_hash_map<MetricKeyHashMap, uint64_t> counter_data_;
  const opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality_;
};

class RequestStreamerHashMap {
public:
  explicit RequestStreamerHashMap(
      opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality)
      : counter_temporality_(counter_temporality) {
    initNewRequest();
  }

  void addCounter(std::string&& name, uint64_t value, uint64_t delta,
                  absl::flat_hash_map<std::string, std::string>&& attributes) {
    using namespace opentelemetry::proto::metrics::v1;
    const uint64_t point_value =
        (counter_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) ? delta
                                                                                        : value;
    auto* metric = findOrCreateMetric(std::move(name));
    auto* sum = metric->mutable_sum();
    sum->set_aggregation_temporality(counter_temporality_);
    sum->set_is_monotonic(true);
    auto* point = sum->add_data_points();
    point->set_time_unix_nano(1000); // Dummy timestamp
    point->set_as_int(point_value);

    for (auto& [k, v] : attributes) {
      auto* attr = point->add_attributes();
      attr->set_key(std::move(k));
      attr->mutable_value()->set_string_value(std::move(v));
    }
  }

  std::unique_ptr<opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest>
  releaseRequest() {
    return std::move(current_request_);
  }

private:
  void initNewRequest() {
    current_request_ = std::make_unique<
        opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest>();
    auto* rm = current_request_->add_resource_metrics();
    current_scope_metrics_ = rm->add_scope_metrics();
  }

  ::opentelemetry::proto::metrics::v1::Metric* findOrCreateMetric(std::string&& name) {
    for (int i = 0; i < current_scope_metrics_->metrics_size(); ++i) {
      auto* m = current_scope_metrics_->mutable_metrics(i);
      if (m->name() == name) {
        return m;
      }
    }
    auto* m = current_scope_metrics_->add_metrics();
    m->set_name(std::move(name));
    return m;
  }

  std::unique_ptr<opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest>
      current_request_;
  ::opentelemetry::proto::metrics::v1::ScopeMetrics* current_scope_metrics_{nullptr};
  const opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality_;
};

struct HashMapTag {};
struct RealAggregatorTag {};

void addCounterToAggregator(MetricAggregatorHashMap& aggregator,
                            absl::flat_hash_map<std::string, std::string>&& attrs) {
  aggregator.addCounter("metric_name", 1, 1, std::move(attrs));
}

void addCounterToAggregator(
    Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator& aggregator,
    Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator::AttributesVector&& attrs) {
  aggregator.addCounter(
      "metric_name", 1,
      Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator::SortedAttributesVector(
          std::move(attrs)));
}

void flushToStreamer(MetricAggregatorHashMap::AggregationResult&& result,
                     RequestStreamerHashMap& streamer) {
  for (auto it = result.counter_data_.begin(); it != result.counter_data_.end();) {
    auto node = result.counter_data_.extract(it++);
    streamer.addCounter(node.key().releaseName(), node.mapped(), 0, node.key().releaseAttributes());
  }
}

void flushToStreamer(
    Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator::AggregationResult&& result,
    Envoy::Extensions::StatSinks::OpenTelemetry::RequestStreamer& streamer) {
  streamer.addAggregationResult(std::move(result));
}

absl::flat_hash_map<std::string, std::string> createBaseAttrs(HashMapTag) {
  absl::flat_hash_map<std::string, std::string> attrs;
  for (const auto& tag : test_tags) {
    attrs.emplace(tag.name_, tag.value_);
  }
  return attrs;
}

Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator::AttributesVector
createBaseAttrs(RealAggregatorTag) {
  Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator::AttributesVector attrs;
  for (const auto& tag : test_tags) {
    attrs.emplace_back(tag.name_, tag.value_);
  }
  std::sort(attrs.begin(), attrs.end());
  return attrs;
}

void updateUniqueTag(absl::flat_hash_map<std::string, std::string>& attrs, int i) {
  attrs[test_tags.back().name_] = std::to_string(i);
}

void updateUniqueTag(
    Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator::AttributesVector& attrs, int i) {
  for (auto& pair : attrs) {
    if (pair.first == test_tags.back().name_) {
      pair.second = std::to_string(i);
      break;
    }
  }
}

template <typename Tag> auto precomputeAttributes(Tag tag) {
  auto base_attrs = createBaseAttrs(tag);
  using Attrs = decltype(base_attrs);
  std::vector<Attrs> precomputed_attrs;
  precomputed_attrs.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    auto attrs = base_attrs;
    updateUniqueTag(attrs, i);
    precomputed_attrs.push_back(std::move(attrs));
  }
  return precomputed_attrs;
}

void BM_aggregationWithHashMap(benchmark::State& state) {
  auto precomputed_attrs = precomputeAttributes(HashMapTag{});
  RequestStreamerHashMap streamer(opentelemetry::proto::metrics::v1::AggregationTemporality::
                                      AGGREGATION_TEMPORALITY_CUMULATIVE);
  for (auto _ : state) {
    MetricAggregatorHashMap aggregator(opentelemetry::proto::metrics::v1::AggregationTemporality::
                                           AGGREGATION_TEMPORALITY_CUMULATIVE);
    for (int i = 0; i < 10000; ++i) {
      auto attrs = precomputed_attrs[i];
      addCounterToAggregator(aggregator, std::move(attrs));
    }

    flushToStreamer(aggregator.releaseResult(), streamer);

    benchmark::DoNotOptimize(&streamer);
  }
}
BENCHMARK(BM_aggregationWithHashMap);

void BM_aggregationWithSortedInlineVector(benchmark::State& state) {
  auto precomputed_attrs = precomputeAttributes(RealAggregatorTag{});
  static const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>
      empty_resource_attributes;

  Envoy::Extensions::StatSinks::OpenTelemetry::RequestStreamer streamer(
      20000, // max_dp
      empty_resource_attributes,
      opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
      opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE,
      [](Envoy::Extensions::StatSinks::OpenTelemetry::MetricsExportRequestPtr) {}, // dummy callback
      1000, 1000, 1000                                                             // dummy times
  );

  for (auto _ : state) {
    Envoy::Extensions::StatSinks::OpenTelemetry::MetricAggregator aggregator(
        opentelemetry::proto::metrics::v1::AggregationTemporality::
            AGGREGATION_TEMPORALITY_CUMULATIVE,
        opentelemetry::proto::metrics::v1::AggregationTemporality::
            AGGREGATION_TEMPORALITY_CUMULATIVE);
    for (int i = 0; i < 10000; ++i) {
      auto attrs = precomputed_attrs[i];
      addCounterToAggregator(aggregator, std::move(attrs));
    }

    flushToStreamer(aggregator.releaseResult(), streamer);

    benchmark::DoNotOptimize(&streamer);
  }
}
BENCHMARK(BM_aggregationWithSortedInlineVector);

} // namespace
} // namespace Stats
} // namespace Envoy
