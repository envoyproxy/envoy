#include "source/extensions/stat_sinks/open_telemetry/metric_aggregator.h"
#include "envoy/common/exception.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using MetricsExportRequest =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using Metric = opentelemetry::proto::metrics::v1::Metric;
using NumberDataPoint = opentelemetry::proto::metrics::v1::NumberDataPoint;
using HistogramDataPoint = opentelemetry::proto::metrics::v1::HistogramDataPoint;
using AggregationTemporality = opentelemetry::proto::metrics::v1::AggregationTemporality;

void MetricAggregator::createNewRequest() {
  current_request_metrics_.clear();
  requests_.push_back(std::make_unique<MetricsExportRequest>());
  auto* rm = requests_.back()->add_resource_metrics();
  for (const auto& attr : resource_attributes_) {
    *rm->mutable_resource()->add_attributes() = attr;
  }
  current_scope_metrics_ = rm->add_scope_metrics();
  cur_dp_num_ = 0;
}

MetricAggregator::MetricData& MetricAggregator::getOrCreateMetric(absl::string_view metric_name) {
  auto metric_name_str = std::string(metric_name);
  auto it = metrics_.find(metric_name_str);
  if (it == metrics_.end()) {
    it = metrics_.emplace(metric_name_str, MetricData{}).first;
  }
  return it->second;
}

Metric* MetricAggregator::getOrCreateMetricInCurrentRequest(absl::string_view metric_name) {
  auto metric_name_str = std::string(metric_name);
  auto it = current_request_metrics_.find(metric_name_str);
  if (it != current_request_metrics_.end()) {
    return it->second;
  }
  Metric* metric = current_scope_metrics_->add_metrics();
  metric->set_name(metric_name_str);
  current_request_metrics_[metric_name_str] = metric;
  return metric;
}

template <typename DataPoint>
void MetricAggregator::setCommonDataPoint(
    DataPoint& data_point,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes,
    AggregationTemporality temporality) const {
  if (temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE) {
    data_point.set_start_time_unix_nano(cumulative_start_time_ns_);
  } else if (temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    data_point.set_start_time_unix_nano(delta_start_time_ns_);
  }
  data_point.set_time_unix_nano(snapshot_time_ns_);

  for (const auto& attr : attributes) {
    *data_point.add_attributes() = attr;
  }
}

void MetricAggregator::addGauge(
    const std::string& metric_name, uint64_t value,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  if (!enable_metric_aggregation_) {
    if (requests_.empty() || (max_dp_ > 0 && cur_dp_num_ >= max_dp_)) {
      createNewRequest();
    }
    cur_dp_num_++;
    Metric* metric = current_scope_metrics_->add_metrics();
    metric->set_name(metric_name);
    NumberDataPoint* data_point = metric->mutable_gauge()->add_data_points();
    setCommonDataPoint(*data_point, attributes,
                       AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED);
    data_point->set_as_int(value);
    return;
  }
  MetricData& metric_data = getOrCreateMetric(metric_name);

  DataPointKey key;
  for (const auto& attr : attributes) {
    key.attributes.emplace(attr.key(), attr.value().string_value());
  }

  auto dp_it = metric_data.gauge_points.find(key);
  if (dp_it != metric_data.gauge_points.end()) {
    dp_it->second->set_as_int(dp_it->second->as_int() + value);
  } else {
    if (requests_.empty() || (max_dp_ > 0 && cur_dp_num_ >= max_dp_)) {
      createNewRequest();
    }
    cur_dp_num_++;
    Metric* metric = getOrCreateMetricInCurrentRequest(metric_name);
    NumberDataPoint* data_point = metric->mutable_gauge()->add_data_points();
    setCommonDataPoint(*data_point, attributes,
                       AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED);
    data_point->set_as_int(value);
    metric_data.gauge_points[key] = data_point;
  }
}

void MetricAggregator::addCounter(
    absl::string_view metric_name, uint64_t value, uint64_t delta,
    AggregationTemporality temporality,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  const uint64_t point_value =
      (temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) ? delta : value;
  if (point_value == 0 && temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }
  if (!enable_metric_aggregation_) {
    if (requests_.empty() || (max_dp_ > 0 && cur_dp_num_ >= max_dp_)) {
      createNewRequest();
    }
    cur_dp_num_++;
    Metric* metric = current_scope_metrics_->add_metrics();
    metric->set_name(std::string(metric_name));
    metric->mutable_sum()->set_is_monotonic(true);
    metric->mutable_sum()->set_aggregation_temporality(temporality);
    NumberDataPoint* data_point = metric->mutable_sum()->add_data_points();
    setCommonDataPoint(*data_point, attributes, temporality);
    data_point->set_as_int(point_value);
    return;
  }
  MetricData& metric_data = getOrCreateMetric(metric_name);

  DataPointKey key;
  for (const auto& attr : attributes) {
    key.attributes.emplace(attr.key(), attr.value().string_value());
  }

  auto dp_it = metric_data.sum_points.find(key);
  if (dp_it != metric_data.sum_points.end()) {
    dp_it->second->set_as_int(dp_it->second->as_int() + point_value);
  } else {
    if (requests_.empty() || (max_dp_ > 0 && cur_dp_num_ >= max_dp_)) {
      createNewRequest();
    }
    cur_dp_num_++;
    Metric* metric = getOrCreateMetricInCurrentRequest(metric_name);
    metric->mutable_sum()->set_is_monotonic(true);
    metric->mutable_sum()->set_aggregation_temporality(temporality);
    NumberDataPoint* data_point = metric->mutable_sum()->add_data_points();
    setCommonDataPoint(*data_point, attributes, temporality);
    data_point->set_as_int(point_value);
    metric_data.sum_points[key] = data_point;
  }
}

void MetricAggregator::addHistogram(
    const std::string& metric_name, const Envoy::Stats::HistogramStatistics& stats,
    AggregationTemporality temporality,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  if (stats.sampleCount() == 0 &&
      temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }

  if (!enable_metric_aggregation_) {
    if (requests_.empty() || (max_dp_ > 0 && cur_dp_num_ >= max_dp_)) {
      createNewRequest();
    }
    cur_dp_num_++;
    Metric* metric = current_scope_metrics_->add_metrics();
    metric->set_name(metric_name);
    metric->mutable_histogram()->set_aggregation_temporality(temporality);
    HistogramDataPoint* data_point = metric->mutable_histogram()->add_data_points();
    setCommonDataPoint(*data_point, attributes, temporality);
    data_point->set_count(stats.sampleCount());
    data_point->set_sum(stats.sampleSum());

    std::vector<uint64_t> bucket_counts = stats.computeDisjointBuckets();
    for (size_t i = 0; i < stats.supportedBuckets().size(); ++i) {
      data_point->add_explicit_bounds(stats.supportedBuckets()[i]);
      data_point->add_bucket_counts(bucket_counts[i]);
    }
    data_point->add_bucket_counts(stats.outOfBoundCount());
    return;
  }

  MetricData& metric_data = getOrCreateMetric(metric_name);

  DataPointKey key;
  for (const auto& attr : attributes) {
    key.attributes.emplace(attr.key(), attr.value().string_value());
  }

  auto dp_it = metric_data.histogram_points.find(key);
  if (dp_it != metric_data.histogram_points.end()) {
    std::vector<uint64_t> new_bucket_counts = stats.computeDisjointBuckets();
    if (static_cast<size_t>(dp_it->second->explicit_bounds_size()) ==
            stats.supportedBuckets().size() &&
        static_cast<size_t>(dp_it->second->bucket_counts_size()) == new_bucket_counts.size() + 1) {
      dp_it->second->set_count(dp_it->second->count() + stats.sampleCount());
      dp_it->second->set_sum(dp_it->second->sum() + stats.sampleSum());
      for (size_t i = 0; i < new_bucket_counts.size(); ++i) {
        dp_it->second->set_bucket_counts(
            i, dp_it->second->bucket_counts(i) + new_bucket_counts[i]);
      }
      dp_it->second->set_bucket_counts(
          new_bucket_counts.size(),
          dp_it->second->bucket_counts(new_bucket_counts.size()) + stats.outOfBoundCount());
    }
  } else {
    if (requests_.empty() || (max_dp_ > 0 && cur_dp_num_ >= max_dp_)) {
      createNewRequest();
    }
    cur_dp_num_++;
    Metric* metric = getOrCreateMetricInCurrentRequest(metric_name);
    metric->mutable_histogram()->set_aggregation_temporality(temporality);
    HistogramDataPoint* data_point = metric->mutable_histogram()->add_data_points();
    setCommonDataPoint(*data_point, attributes, temporality);
    data_point->set_count(stats.sampleCount());
    data_point->set_sum(stats.sampleSum());

    std::vector<uint64_t> bucket_counts = stats.computeDisjointBuckets();
    for (size_t i = 0; i < stats.supportedBuckets().size(); ++i) {
      data_point->add_explicit_bounds(stats.supportedBuckets()[i]);
      data_point->add_bucket_counts(bucket_counts[i]);
    }
    data_point->add_bucket_counts(stats.outOfBoundCount());
    metric_data.histogram_points[key] = data_point;
  }
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
