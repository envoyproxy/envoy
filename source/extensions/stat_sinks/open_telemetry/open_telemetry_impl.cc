#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/request_splitter.h"
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
                       config_->maxDatapointsPerRequest(),
                       config_->resource_attributes());

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
      aggregator.addHistogram(metric_name, histogram_stats,
                              histogram_temporality, attributes);
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
