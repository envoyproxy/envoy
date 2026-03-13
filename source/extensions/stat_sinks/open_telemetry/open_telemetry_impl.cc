#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/stat_match_action.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using ::opentelemetry::proto::metrics::v1::AggregationTemporality;
using ::opentelemetry::proto::metrics::v1::HistogramDataPoint;
using ::opentelemetry::proto::metrics::v1::Metric;
using ::opentelemetry::proto::metrics::v1::NumberDataPoint;
using ::opentelemetry::proto::metrics::v1::ResourceMetrics;

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
    for (size_t i = 0; i < stats.supportedBuckets().size(); i++) {
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
        dp_it->second->set_bucket_counts(i, dp_it->second->bucket_counts(i) + new_bucket_counts[i]);
      }
      dp_it->second->set_bucket_counts(new_bucket_counts.size(),
                                       dp_it->second->bucket_counts(new_bucket_counts.size()) +
                                           stats.outOfBoundCount());
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

Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>
generateResourceAttributes(const Tracers::OpenTelemetry::Resource& resource) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> resource_attributes;
  for (const auto& attr : resource.attributes_) {
    auto* attribute = resource_attributes.Add();
    attribute->set_key(attr.first);
    attribute->mutable_value()->set_string_value(attr.second);
  }
  return resource_attributes;
}

Matcher::MatchTreePtr<Stats::StatMatchingData>
createMatcher(const xds::type::matcher::v3::Matcher& matcher_config,
              Server::Configuration::ServerFactoryContext& server_factory_context) {
  ActionValidationVisitor validation_visitor;
  ActionContext action_context;
  Matcher::MatchTreeFactory<Stats::StatMatchingData, ActionContext> factory{
      action_context, server_factory_context, validation_visitor};
  return factory.create(matcher_config)();
}

OtlpOptions::OtlpOptions(const SinkConfig& sink_config,
                         const Tracers::OpenTelemetry::Resource& resource,
                         Server::Configuration::ServerFactoryContext& server)
    : report_counters_as_deltas_(sink_config.report_counters_as_deltas()),
      report_histograms_as_deltas_(sink_config.report_histograms_as_deltas()),
      emit_tags_as_attributes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, emit_tags_as_attributes, true)),
      use_tag_extracted_name_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, use_tag_extracted_name, true)),
      stat_prefix_(!sink_config.prefix().empty() ? sink_config.prefix() + "." : ""),
      enable_metric_aggregation_(sink_config.has_custom_metric_conversions()),
      resource_attributes_(generateResourceAttributes(resource)),
      matcher_(createMatcher(sink_config.custom_metric_conversions(), server)),
      max_datapoints_per_request_(sink_config.max_datapoints_per_request()) {}

OpenTelemetryGrpcMetricsExporterImpl::OpenTelemetryGrpcMetricsExporterImpl(
    const OtlpOptionsSharedPtr config, Grpc::RawAsyncClientSharedPtr raw_async_client)
    : config_(config), client_(raw_async_client),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "opentelemetry.proto.collector.metrics.v1.MetricsService."
          "Export")) {}

void OpenTelemetryGrpcMetricsExporterImpl::send(MetricsExportRequestPtr&& export_request) {
  ENVOY_LOG(debug, "sending a OTLP metric request: {}", export_request->DebugString());
  client_->send(service_method_, *export_request, *this, Tracing::NullSpan::instance(),
                Http::AsyncClient::RequestOptions());
}

void OpenTelemetryGrpcMetricsExporterImpl::onSuccess(
    Grpc::ResponsePtr<MetricsExportResponse>&& export_response, Tracing::Span&) {
  if (export_response->has_partial_success()) {
    ENVOY_LOG(debug,
              "export response with partial success; {} rejected, collector "
              "message: {}",
              export_response->partial_success().rejected_data_points(),
              export_response->partial_success().error_message());
  }
}

void OpenTelemetryGrpcMetricsExporterImpl::onFailure(Grpc::Status::GrpcStatus response_status,
                                                     const std::string& response_message,
                                                     Tracing::Span&) {
  ENVOY_LOG(debug, "export failure; status: {}, message: {}", response_status, response_message);
}

template <class StatType>
OtlpMetricsFlusherImpl::MetricConfig
OtlpMetricsFlusherImpl::getMetricConfig(const StatType& stat) const {
  Stats::StatMatchingDataImpl<StatType> data(stat);
  const ::Envoy::Matcher::ActionMatchResult result =
      Envoy::Matcher::evaluateMatch<Stats::StatMatchingData>(*config_->matcher(), data);
  ASSERT(result.isComplete());
  if (result.isMatch()) {
    if (dynamic_cast<const DropAction*>(result.action().get())) {
      return {true, {}};
    }

    if (const auto* match_action = dynamic_cast<const ConversionAction*>(result.action().get())) {
      return {false, *match_action->config()};
    }

    ENVOY_LOG(error, "Unknown action type for custom metric conversion: {}",
              result.action()->typeUrl());
  }

  // By default, this stat will be converted to the metric without any
  // customization.
  return {false, {}};
}

template <class StatType>
std::string OtlpMetricsFlusherImpl::getMetricName(
    const StatType& stat, OptRef<const SinkConfig::ConversionAction> conversion_config) const {
  if (conversion_config.has_value()) {
    return conversion_config->metric_name();
  }
  return absl::StrCat(config_->statPrefix(),
                      config_->useTagExtractedName() ? stat.tagExtractedName() : stat.name());
}

template <class StatType>
Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>
OtlpMetricsFlusherImpl::getCombinedAttributes(
    const StatType& stat, OptRef<const SinkConfig::ConversionAction> conversion_config) const {
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attributes;
  if (config_->emitTagsAsAttributes()) {
    for (const auto& tag : stat.tags()) {
      auto* attribute = attributes.Add();
      attribute->set_key(tag.name_);
      attribute->mutable_value()->set_string_value(tag.value_);
    }
  }
  if (conversion_config.has_value()) {
    for (const auto& attr : conversion_config->static_metric_labels()) {
      *attributes.Add() = attr;
    }
  }
  return attributes;
}

void OtlpMetricsFlusherImpl::flush(
    Stats::MetricSnapshot& snapshot, int64_t delta_start_time_ns, int64_t cumulative_start_time_ns,
    absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) const {
  MetricAggregator aggregator =
      MetricAggregator(config_->enableMetricAggregation(),
                       std::chrono::duration_cast<std::chrono::nanoseconds>(
                           snapshot.snapshotTime().time_since_epoch())
                           .count(),
                       delta_start_time_ns, cumulative_start_time_ns,
                       config_->maxDatapointsPerRequest(), config_->resource_attributes());

  // Process Gauges
  for (const auto& gauge : snapshot.gauges()) {
    if (predicate_(gauge)) {
      auto metric_config = getMetricConfig(gauge.get());
      if (metric_config.drop_stat) {
        continue;
      }

      const std::string metric_name = getMetricName(gauge.get(), metric_config.conversion_action);
      auto attributes = getCombinedAttributes(gauge.get(), metric_config.conversion_action);
      aggregator.addGauge(metric_name, gauge.get().value(), attributes);
    };
  }
  for (const auto& gauge : snapshot.hostGauges()) {
    auto metric_config = getMetricConfig(gauge);
    if (metric_config.drop_stat) {
      continue;
    }

    const std::string metric_name = getMetricName(gauge, metric_config.conversion_action);
    auto attributes = getCombinedAttributes(gauge, metric_config.conversion_action);
    aggregator.addGauge(metric_name, gauge.value(), attributes);
  }

  // Process Counters
  AggregationTemporality counter_temporality =
      config_->reportCountersAsDeltas()
          ? AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA
          : AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE;
  for (const auto& counter : snapshot.counters()) {
    if (predicate_(counter.counter_)) {
      auto metric_config = getMetricConfig(counter.counter_.get());
      if (metric_config.drop_stat) {
        continue;
      }

      const std::string metric_name =
          getMetricName(counter.counter_.get(), metric_config.conversion_action);
      auto attributes =
          getCombinedAttributes(counter.counter_.get(), metric_config.conversion_action);
      aggregator.addCounter(metric_name, counter.counter_.get().value(), counter.delta_,
                            counter_temporality, attributes);
    }
  }
  for (const auto& counter : snapshot.hostCounters()) {
    auto metric_config = getMetricConfig(counter);
    if (metric_config.drop_stat) {
      continue;
    }

    const std::string metric_name = getMetricName(counter, metric_config.conversion_action);
    auto attributes = getCombinedAttributes(counter, metric_config.conversion_action);
    aggregator.addCounter(metric_name, counter.value(), counter.delta(), counter_temporality,
                          attributes);
  }

  // Process Histograms
  AggregationTemporality histogram_temporality =
      config_->reportHistogramsAsDeltas()
          ? AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA
          : AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE;
  for (const auto& histogram : snapshot.histograms()) {
    if (predicate_(histogram)) {
      auto metric_config = getMetricConfig(histogram.get());
      if (metric_config.drop_stat) {
        continue;
      }

      const std::string metric_name =
          getMetricName(histogram.get(), metric_config.conversion_action);
      auto attributes = getCombinedAttributes(histogram.get(), metric_config.conversion_action);
      const Stats::HistogramStatistics& histogram_stats =
          config_->reportHistogramsAsDeltas() ? histogram.get().intervalStatistics()
                                              : histogram.get().cumulativeStatistics();
      aggregator.addHistogram(metric_name, histogram_stats, histogram_temporality, attributes);
    }
  }

  for (auto& request : aggregator.releaseRequests()) {
    send_callback(std::move(request));
  }
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
