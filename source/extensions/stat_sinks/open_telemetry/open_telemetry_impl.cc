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

MetricAggregator::AggregationResult MetricAggregator::releaseResult() {
  return {std::move(gauge_data_), std::move(counter_data_),  std::move(histogram_data_),
          snapshot_time_ns_,      cumulative_start_time_ns_, delta_start_time_ns_};
}

void OtlpRequestStreamer::streamRequests(
    MetricAggregator::AggregationResult& metrics,
    absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) const {
  std::unique_ptr<MetricsExportRequest> current_request;
  opentelemetry::proto::metrics::v1::ScopeMetrics* current_scope_metrics = nullptr;
  uint32_t dp_num = 0;
  absl::flat_hash_map<std::string, opentelemetry::proto::metrics::v1::Metric*> name_to_metric;

  auto ensureRequest = [&]() {
    if (current_request != nullptr && (max_dp_ == 0 || dp_num < max_dp_)) {
      return;
    }
    if (current_request != nullptr) {
      send_callback(std::move(current_request));
    }
    current_request = std::make_unique<MetricsExportRequest>();
    auto* rm = current_request->add_resource_metrics();
    for (const auto& attr : resource_attributes_) {
      *rm->mutable_resource()->add_attributes() = attr;
    }
    current_scope_metrics = rm->add_scope_metrics();
    name_to_metric.clear();
    dp_num = 0;
  };

  auto find_or_create_metric = [&](const std::string& name) -> Metric* {
    if (!enable_metric_aggregation_) {
      Metric* m = current_scope_metrics->add_metrics();
      m->set_name(name);
      return m;
    }
    auto it = name_to_metric.find(name);
    if (it != name_to_metric.end()) {
      return it->second;
    }
    Metric* m = current_scope_metrics->add_metrics();
    m->set_name(name);
    name_to_metric[name] = m;
    return m;
  };

  for (const auto& [key, value] : metrics.gauge_data_) {
    ensureRequest();
    Metric* metric = find_or_create_metric(key.name_);
    auto* point = metric->mutable_gauge()->add_data_points();
    point->set_as_int(value);
    setCommonFields(point, key, AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED,
                    metrics);
    dp_num++;
  }

  for (const auto& [key, value] : metrics.counter_data_) {
    ensureRequest();
    Metric* metric = find_or_create_metric(key.name_);
    AggregationTemporality temp = counter_temporality_;
    if (metric->mutable_sum()->data_points_size() == 0) {
      metric->mutable_sum()->set_is_monotonic(true);
      metric->mutable_sum()->set_aggregation_temporality(temp);
    }
    auto* point = metric->mutable_sum()->add_data_points();
    point->set_as_int(value);
    setCommonFields(point, key, temp, metrics);
    dp_num++;
  }

  for (const auto& [key, custom_hist] : metrics.histogram_data_) {
    ensureRequest();
    Metric* metric = find_or_create_metric(key.name_);
    AggregationTemporality temp = histogram_temporality_;
    if (metric->mutable_histogram()->data_points_size() == 0) {
      metric->mutable_histogram()->set_aggregation_temporality(temp);
    }
    auto* point = metric->mutable_histogram()->add_data_points();
    point->set_count(custom_hist.count_);
    point->set_sum(custom_hist.sum_);
    for (double bound : custom_hist.explicit_bounds_) {
      point->add_explicit_bounds(bound);
    }
    for (uint64_t count : custom_hist.bucket_counts_) {
      point->add_bucket_counts(count);
    }
    setCommonFields(point, key, temp, metrics);
    dp_num++;
  }
  if (current_request != nullptr) {
    send_callback(std::move(current_request));
  }
}

template <class PointType>
void OtlpRequestStreamer::setCommonFields(
    PointType* point, const MetricAggregator::MetricKey& key,
    opentelemetry::proto::metrics::v1::AggregationTemporality temp,
    const MetricAggregator::AggregationResult& metrics) const {
  point->set_time_unix_nano(metrics.snapshot_time_ns_);
  if (temp == opentelemetry::proto::metrics::v1::AggregationTemporality::
                  AGGREGATION_TEMPORALITY_CUMULATIVE) {
    point->set_start_time_unix_nano(metrics.cumulative_start_time_ns_);
  } else if (temp == opentelemetry::proto::metrics::v1::AggregationTemporality::
                         AGGREGATION_TEMPORALITY_DELTA) {
    point->set_start_time_unix_nano(metrics.delta_start_time_ns_);
  }

  for (const auto& [k, v] : key.attributes_) {
    auto* attr = point->add_attributes();
    attr->set_key(k);
    attr->mutable_value()->set_string_value(v);
  }
}

void MetricAggregator::addGauge(
    absl::string_view metric_name, uint64_t value,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  MetricKey key{metric_name, attributes};

  auto it = gauge_data_.find(key);
  if (it != gauge_data_.end() && enable_metric_aggregation_) {
    it->second += value;
  } else {
    gauge_data_[key] = value;
  }
}

void MetricAggregator::addCounter(
    absl::string_view metric_name, uint64_t value, uint64_t delta,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  const uint64_t point_value =
      (counter_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) ? delta
                                                                                      : value;
  if (point_value == 0 &&
      counter_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }

  MetricKey key{metric_name, attributes};

  auto it = counter_data_.find(key);
  if (it != counter_data_.end() && enable_metric_aggregation_) {
    it->second += point_value;
  } else {
    counter_data_[key] = point_value;
  }
}

void MetricAggregator::addHistogram(
    absl::string_view metric_name, const Envoy::Stats::HistogramStatistics& stats,
    const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes) {
  if (stats.sampleCount() == 0 &&
      histogram_temporality_ == AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA) {
    return;
  }

  MetricKey key{metric_name, attributes};

  auto it = histogram_data_.find(key);
  if (it != histogram_data_.end() && enable_metric_aggregation_) {
    std::vector<uint64_t> new_bucket_counts = stats.computeDisjointBuckets();
    auto& existing_point = it->second;
    if (existing_point.explicit_bounds_.size() == stats.supportedBuckets().size() &&
        existing_point.bucket_counts_.size() == new_bucket_counts.size() + 1) {
      existing_point.count_ += stats.sampleCount();
      existing_point.sum_ += stats.sampleSum();
      for (size_t i = 0; i < new_bucket_counts.size(); ++i) {
        existing_point.bucket_counts_[i] += new_bucket_counts[i];
      }
      existing_point.bucket_counts_.back() += stats.outOfBoundCount();
    } else {
      ENVOY_LOG(error, "Histogram bounds mismatch for metric {} aggregated from stat", metric_name);
    }
  } else {
    CustomHistogram custom_hist;
    custom_hist.count_ = stats.sampleCount();
    custom_hist.sum_ = stats.sampleSum();
    custom_hist.bucket_counts_ = stats.computeDisjointBuckets();
    custom_hist.bucket_counts_.push_back(stats.outOfBoundCount());
    custom_hist.explicit_bounds_ = stats.supportedBuckets();
    histogram_data_[key] = std::move(custom_hist);
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
      max_data_points_per_request_(sink_config.max_data_points_per_request()) {}

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
  AggregationTemporality counter_temporality =
      config_->reportCountersAsDeltas()
          ? AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA
          : AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE;
  AggregationTemporality histogram_temporality =
      config_->reportHistogramsAsDeltas()
          ? AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA
          : AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE;

  MetricAggregator aggregator = MetricAggregator(
      config_->enableMetricAggregation(),
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          snapshot.snapshotTime().time_since_epoch())
          .count(),
      delta_start_time_ns, cumulative_start_time_ns, counter_temporality, histogram_temporality);
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
                            attributes);
    }
  }
  for (const auto& counter : snapshot.hostCounters()) {
    auto metric_config = getMetricConfig(counter);
    if (metric_config.drop_stat) {
      continue;
    }

    const std::string metric_name = getMetricName(counter, metric_config.conversion_action);
    auto attributes = getCombinedAttributes(counter, metric_config.conversion_action);
    aggregator.addCounter(metric_name, counter.value(), counter.delta(), attributes);
  }

  // Process Histograms

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
      aggregator.addHistogram(metric_name, histogram_stats, attributes);
    }
  }

  auto metrics = aggregator.releaseResult();
  streamer_.streamRequests(metrics, std::move(send_callback));
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
