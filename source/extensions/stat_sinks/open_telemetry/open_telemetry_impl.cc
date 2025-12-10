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

MetricAggregator::AttributesMap MetricAggregator::GetAttributesMap(
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attrs) {
  AttributesMap map;
  for (const auto& attr : attrs) {
    map[attr.key()] = attr.value().string_value();
  }
  return map;
}

MetricAggregator::MetricData& MetricAggregator::getOrCreateMetric(absl::string_view metric_name) {
  auto& metric_data = metrics_[metric_name];
  if (metric_data.metric.name().empty()) {
    metric_data.metric.set_name(metric_name);
  }
  return metric_data;
}

void MetricAggregator::addGauge(
    absl::string_view metric_name, int64_t value,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  if (!enable_metric_aggregation_) {
    Metric metric;
    metric.set_name(metric_name);
    NumberDataPoint* data_point = metric.mutable_gauge()->add_data_points();
    setCommonDataPoint(*data_point, &attributes,
                       AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED);
    data_point->set_as_int(value);
    non_aggregated_metrics_.push_back(std::move(metric));
    return;
  }

  MetricData& metric_data = getOrCreateMetric(metric_name);
  DataPointKey key{GetAttributesMap(attributes)};

  auto it = metric_data.gauge_points.find(key);
  if (it != metric_data.gauge_points.end()) {
    // If the data point exists, update it and return.
    NumberDataPoint* data_point = it->second;
    setCommonDataPoint(*data_point, nullptr,
                       AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED);

    // Multiple stats are mapped to the same metric and we
    // aggregate by summing the new value to the existing one.
    data_point->set_as_int(data_point->as_int() + value);
    return;
  }

  // If the data point does not exist, create a new one.
  NumberDataPoint* data_point = metric_data.metric.mutable_gauge()->add_data_points();
  metric_data.gauge_points[key] = data_point;
  setCommonDataPoint(*data_point, &attributes,
                     AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED);
  data_point->set_as_int(value);
}

void MetricAggregator::addCounter(
    absl::string_view metric_name, uint64_t value, uint64_t delta,
    AggregationTemporality temporality,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  if (!enable_metric_aggregation_) {
    Metric metric;
    metric.set_name(metric_name);
    metric.mutable_sum()->set_is_monotonic(true);
    metric.mutable_sum()->set_aggregation_temporality(temporality);
    NumberDataPoint* data_point = metric.mutable_sum()->add_data_points();
    setCommonDataPoint(*data_point, &attributes, temporality);
    data_point->set_as_int(
        (temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) ? delta : value);
    non_aggregated_metrics_.push_back(std::move(metric));
    return;
  }
  MetricData& metric_data = getOrCreateMetric(metric_name);

  DataPointKey key{GetAttributesMap(attributes)};
  auto it = metric_data.counter_points.find(key);
  if (it != metric_data.counter_points.end()) {
    // If the data point exists, update it and return.
    NumberDataPoint* data_point = it->second;
    // Update time for the existing data point.
    setCommonDataPoint(*data_point, nullptr, temporality);

    // For DELTA, add the change since the last export. For CUMULATIVE, add the
    // total value.
    data_point->set_as_int(
        data_point->as_int() +
        ((temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) ? delta : value));
    return;
  }

  // If the data point does not exist, create a new one.
  NumberDataPoint* data_point = metric_data.metric.mutable_sum()->add_data_points();
  metric_data.metric.mutable_sum()->set_is_monotonic(true);
  metric_data.metric.mutable_sum()->set_aggregation_temporality(temporality);
  metric_data.counter_points[key] = data_point;
  setCommonDataPoint(*data_point, &attributes, temporality);
  data_point->set_as_int(
      (temporality == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) ? delta : value);
}

void MetricAggregator::addHistogram(
    absl::string_view stat_name, absl::string_view metric_name,
    const Stats::HistogramStatistics& stats, AggregationTemporality temporality,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  if (!enable_metric_aggregation_) {
    Metric metric;
    metric.set_name(metric_name);
    metric.mutable_histogram()->set_aggregation_temporality(temporality);
    HistogramDataPoint* data_point = metric.mutable_histogram()->add_data_points();
    setCommonDataPoint(*data_point, &attributes, temporality);

    data_point->set_count(stats.sampleCount());
    data_point->set_sum(stats.sampleSum());

    std::vector<uint64_t> bucket_counts = stats.computeDisjointBuckets();
    for (size_t i = 0; i < stats.supportedBuckets().size(); i++) {
      data_point->add_explicit_bounds(stats.supportedBuckets()[i]);
      data_point->add_bucket_counts(bucket_counts[i]);
    }
    data_point->add_bucket_counts(stats.outOfBoundCount());
    non_aggregated_metrics_.push_back(std::move(metric));
    return;
  }
  MetricData& metric_data = getOrCreateMetric(metric_name);

  DataPointKey key{GetAttributesMap(attributes)};
  auto it = metric_data.histogram_points.find(key);
  if (it != metric_data.histogram_points.end()) {
    // If the data point exists, update it and return.
    HistogramDataPoint* data_point = it->second;
    std::vector<uint64_t> new_bucket_counts = stats.computeDisjointBuckets();
    if (static_cast<size_t>(data_point->explicit_bounds_size()) ==
            stats.supportedBuckets().size() &&
        static_cast<size_t>(data_point->bucket_counts_size()) == new_bucket_counts.size() + 1) {

      // Update time.
      setCommonDataPoint(*data_point, nullptr, temporality);

      // Aggregate count and sum.
      data_point->set_count(data_point->count() + stats.sampleCount());
      data_point->set_sum(data_point->sum() + stats.sampleSum());

      // Aggregate bucket_counts.
      for (size_t i = 0; i < new_bucket_counts.size(); ++i) {
        data_point->set_bucket_counts(i, data_point->bucket_counts(i) + new_bucket_counts[i]);
      }
      data_point->set_bucket_counts(new_bucket_counts.size(),
                                    data_point->bucket_counts(new_bucket_counts.size()) +
                                        stats.outOfBoundCount());
    } else {
      ENVOY_LOG(error, "Histogram bounds mismatch for metric {} aggregated from stat {}",
                metric_name, stat_name);
    }
    return;
  }

  // If the data point does not exist, create a new one.
  HistogramDataPoint* data_point = metric_data.metric.mutable_histogram()->add_data_points();
  metric_data.metric.mutable_histogram()->set_aggregation_temporality(temporality);
  metric_data.histogram_points[key] = data_point;
  // Set common fields directly here
  setCommonDataPoint(*data_point, &attributes, temporality);

  data_point->set_count(stats.sampleCount());
  data_point->set_sum(stats.sampleSum());
  // TODO(ohadvano): support min/max optional fields for
  // ``HistogramDataPoint``

  std::vector<uint64_t> bucket_counts = stats.computeDisjointBuckets();
  for (size_t i = 0; i < stats.supportedBuckets().size(); i++) {
    data_point->add_explicit_bounds(stats.supportedBuckets()[i]);
    data_point->add_bucket_counts(bucket_counts[i]);
  }
  data_point->add_bucket_counts(stats.outOfBoundCount());
}

Protobuf::RepeatedPtrField<ResourceMetrics> MetricAggregator::getResourceMetrics(
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
        resource_attributes) const {
  Protobuf::RepeatedPtrField<ResourceMetrics> resource_metrics_list;
  if (metrics_.empty() && non_aggregated_metrics_.empty()) {
    return resource_metrics_list;
  }

  auto* resource_metrics = resource_metrics_list.Add();
  resource_metrics->mutable_resource()->mutable_attributes()->CopyFrom(resource_attributes);
  auto* scope_metrics = resource_metrics->add_scope_metrics();

  for (auto const& [key, metric_data] : metrics_) {
    *scope_metrics->add_metrics() = metric_data.metric;
  }
  for (const auto& metric : non_aggregated_metrics_) {
    *scope_metrics->add_metrics() = metric;
  }
  return resource_metrics_list;
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
      matcher_(createMatcher(sink_config.custom_metric_conversions(), server)) {}

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
  const ::Envoy::Matcher::MatchResult result =
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

MetricsExportRequestPtr OtlpMetricsFlusherImpl::flush(Stats::MetricSnapshot& snapshot,
                                                      int64_t delta_start_time_ns,
                                                      int64_t cumulative_start_time_ns) const {
  auto request = std::make_unique<MetricsExportRequest>();
  MetricAggregator aggregator =
      MetricAggregator(config_->enableMetricAggregation(),
                       std::chrono::duration_cast<std::chrono::nanoseconds>(
                           snapshot.snapshotTime().time_since_epoch())
                           .count(),
                       delta_start_time_ns, cumulative_start_time_ns);

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
      aggregator.addHistogram(histogram.get().name(), metric_name, histogram_stats,
                              histogram_temporality, attributes);
    }
  }
  // Add all aggregated metrics to the request.
  *request->mutable_resource_metrics() =
      aggregator.getResourceMetrics(config_->resource_attributes());
  return request;
}
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
