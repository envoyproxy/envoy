#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

OtlpOptions::OtlpOptions(const SinkConfig& sink_config)
    : report_counters_as_deltas_(sink_config.report_counters_as_deltas()),
      report_histograms_as_deltas_(sink_config.report_histograms_as_deltas()),
      emit_tags_as_attributes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, emit_tags_as_attributes, true)),
      use_tag_extracted_name_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, use_tag_extracted_name, true)),
      stat_prefix_(!sink_config.prefix().empty() ? sink_config.prefix() + "." : "") {}

OpenTelemetryGrpcMetricsExporterImpl::OpenTelemetryGrpcMetricsExporterImpl(
    const OtlpOptionsSharedPtr config, Grpc::RawAsyncClientSharedPtr raw_async_client)
    : config_(config), client_(raw_async_client),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "opentelemetry.proto.collector.metrics.v1.MetricsService.Export")) {}

void OpenTelemetryGrpcMetricsExporterImpl::send(MetricsExportRequestPtr&& export_request) {
  client_->send(service_method_, *export_request, *this, Tracing::NullSpan::instance(),
                Http::AsyncClient::RequestOptions());
}

void OpenTelemetryGrpcMetricsExporterImpl::onSuccess(
    Grpc::ResponsePtr<MetricsExportResponse>&& export_response, Tracing::Span&) {
  if (export_response->has_partial_success()) {
    ENVOY_LOG(debug, "export response with partial success; {} rejected, collector message: {}",
              export_response->partial_success().rejected_data_points(),
              export_response->partial_success().error_message());
  }
}

void OpenTelemetryGrpcMetricsExporterImpl::onFailure(Grpc::Status::GrpcStatus response_status,
                                                     const std::string& response_message,
                                                     Tracing::Span&) {
  ENVOY_LOG(debug, "export failure; status: {}, message: {}", response_status, response_message);
}

MetricsExportRequestPtr OtlpMetricsFlusherImpl::flush(Stats::MetricSnapshot& snapshot) const {
  auto request = std::make_unique<MetricsExportRequest>();
  auto* resource_metrics = request->add_resource_metrics();
  auto* scope_metrics = resource_metrics->add_scope_metrics();

  int64_t snapshot_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 snapshot.snapshotTime().time_since_epoch())
                                 .count();

  for (const auto& gauge : snapshot.gauges()) {
    if (predicate_(gauge)) {
      flushGauge(*scope_metrics->add_metrics(), gauge.get(), snapshot_time_ns);
    }
  }

  for (const auto& gauge : snapshot.hostGauges()) {
    flushGauge(*scope_metrics->add_metrics(), gauge, snapshot_time_ns);
  }

  for (const auto& counter : snapshot.counters()) {
    if (predicate_(counter.counter_)) {
      flushCounter(*scope_metrics->add_metrics(), counter.counter_.get(),
                   counter.counter_.get().value(), counter.delta_, snapshot_time_ns);
    }
  }

  for (const auto& counter : snapshot.hostCounters()) {
    flushCounter(*scope_metrics->add_metrics(), counter, counter.value(), counter.delta(),
                 snapshot_time_ns);
  }

  for (const auto& histogram : snapshot.histograms()) {
    if (predicate_(histogram)) {
      flushHistogram(*scope_metrics->add_metrics(), histogram, snapshot_time_ns);
    }
  }

  return request;
}

template <class GaugeType>
void OtlpMetricsFlusherImpl::flushGauge(opentelemetry::proto::metrics::v1::Metric& metric,
                                        const GaugeType& gauge_stat,
                                        int64_t snapshot_time_ns) const {
  auto* data_point = metric.mutable_gauge()->add_data_points();
  data_point->set_time_unix_nano(snapshot_time_ns);
  setMetricCommon(metric, *data_point, snapshot_time_ns, gauge_stat);

  data_point->set_as_int(gauge_stat.value());
}

template <class CounterType>
void OtlpMetricsFlusherImpl::flushCounter(opentelemetry::proto::metrics::v1::Metric& metric,
                                          const CounterType& counter, uint64_t value,
                                          uint64_t delta, int64_t snapshot_time_ns) const {
  auto* sum = metric.mutable_sum();
  sum->set_is_monotonic(true);
  auto* data_point = sum->add_data_points();
  setMetricCommon(metric, *data_point, snapshot_time_ns, counter);

  if (config_->reportCountersAsDeltas()) {
    sum->set_aggregation_temporality(AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA);
    data_point->set_as_int(delta);
  } else {
    sum->set_aggregation_temporality(AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);
    data_point->set_as_int(value);
  }
}

void OtlpMetricsFlusherImpl::flushHistogram(opentelemetry::proto::metrics::v1::Metric& metric,
                                            const Stats::ParentHistogram& parent_histogram,
                                            int64_t snapshot_time_ns) const {
  auto* histogram = metric.mutable_histogram();
  auto* data_point = histogram->add_data_points();
  setMetricCommon(metric, *data_point, snapshot_time_ns, parent_histogram);

  histogram->set_aggregation_temporality(
      config_->reportHistogramsAsDeltas()
          ? AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA
          : AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE);

  const Stats::HistogramStatistics& histogram_stats = config_->reportHistogramsAsDeltas()
                                                          ? parent_histogram.intervalStatistics()
                                                          : parent_histogram.cumulativeStatistics();

  data_point->set_count(histogram_stats.sampleCount());
  data_point->set_sum(histogram_stats.sampleSum());
  // TODO(ohadvano): support min/max optional fields for ``HistogramDataPoint``

  std::vector<uint64_t> bucket_counts = histogram_stats.computeDisjointBuckets();
  for (size_t i = 0; i < histogram_stats.supportedBuckets().size(); i++) {
    data_point->add_explicit_bounds(histogram_stats.supportedBuckets()[i]);
    data_point->add_bucket_counts(bucket_counts[i]);
  }

  // According to the spec, the number of bucket counts needs to be one element bigger
  // than the size of the explicit bounds, and the last bucket should contain the count
  // of values which are outside the explicit boundaries (to +infinity).
  data_point->add_bucket_counts(histogram_stats.outOfBoundCount());
}

template <class StatType>
void OtlpMetricsFlusherImpl::setMetricCommon(
    opentelemetry::proto::metrics::v1::Metric& metric,
    opentelemetry::proto::metrics::v1::NumberDataPoint& data_point, int64_t snapshot_time_ns,
    const StatType& stat) const {
  data_point.set_time_unix_nano(snapshot_time_ns);
  // TODO(ohadvano): support ``start_time_unix_nano`` optional field
  metric.set_name(absl::StrCat(config_->statPrefix(), config_->useTagExtractedName()
                                                          ? stat.tagExtractedName()
                                                          : stat.name()));

  if (config_->emitTagsAsAttributes()) {
    for (const auto& tag : stat.tags()) {
      auto* attribute = data_point.add_attributes();
      attribute->set_key(tag.name_);
      attribute->mutable_value()->set_string_value(tag.value_);
    }
  }
}

void OtlpMetricsFlusherImpl::setMetricCommon(
    opentelemetry::proto::metrics::v1::Metric& metric,
    opentelemetry::proto::metrics::v1::HistogramDataPoint& data_point, int64_t snapshot_time_ns,
    const Stats::Metric& stat) const {
  data_point.set_time_unix_nano(snapshot_time_ns);
  // TODO(ohadvano): support ``start_time_unix_nano optional`` field
  metric.set_name(absl::StrCat(config_->statPrefix(), config_->useTagExtractedName()
                                                          ? stat.tagExtractedName()
                                                          : stat.name()));

  if (config_->emitTagsAsAttributes()) {
    for (const auto& tag : stat.tags()) {
      auto* attribute = data_point.add_attributes();
      attribute->set_key(tag.name_);
      attribute->mutable_value()->set_string_value(tag.value_);
    }
  }
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
