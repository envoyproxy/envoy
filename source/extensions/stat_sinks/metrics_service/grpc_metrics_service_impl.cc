#include "source/extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/config/metrics/v3/metrics_service.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/metrics/v3/metrics_service.pb.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

GrpcMetricsStreamerImpl::GrpcMetricsStreamerImpl(Grpc::RawAsyncClientSharedPtr raw_async_client,
                                                 const LocalInfo::LocalInfo& local_info)
    : GrpcMetricsStreamer<envoy::service::metrics::v3::StreamMetricsMessage,
                          envoy::service::metrics::v3::StreamMetricsResponse>(raw_async_client),
      local_info_(local_info),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.metrics.v3.MetricsService.StreamMetrics")) {}

void GrpcMetricsStreamerImpl::send(MetricsPtr&& metrics) {
  envoy::service::metrics::v3::StreamMetricsMessage message;
  message.mutable_envoy_metrics()->Reserve(metrics->size());
  message.mutable_envoy_metrics()->MergeFrom(*metrics);

  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "Establishing new gRPC metrics service stream");
    stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
    // For perf reasons, the identifier is only sent on establishing the stream.
    auto* identifier = message.mutable_identifier();
    *identifier->mutable_node() = local_info_.node();
  }
  if (stream_ == nullptr) {
    ENVOY_LOG(error,
              "unable to establish metrics service stream. Will retry in the next flush cycle");
    return;
  }
  stream_->sendMessage(message, false);
}

MetricsPtr MetricsFlusher::flush(Stats::MetricSnapshot& snapshot) const {
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();

  // TODO(mrice32): there's probably some more sophisticated preallocation we can do here where we
  // actually preallocate the submessages and then pass ownership to the proto (rather than just
  // preallocating the pointer array).
  metrics->Reserve(snapshot.counters().size() + snapshot.gauges().size() +
                   snapshot.histograms().size());
  int64_t snapshot_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 snapshot.snapshotTime().time_since_epoch())
                                 .count();
  for (const auto& counter : snapshot.counters()) {
    if (predicate_(counter.counter_.get())) {
      flushCounter(*metrics->Add(), counter, snapshot_time_ms);
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (predicate_(gauge)) {
      flushGauge(*metrics->Add(), gauge.get(), snapshot_time_ms);
    }
  }

  for (const auto& histogram : snapshot.histograms()) {
    if (predicate_(histogram.get())) {
      if (emit_summary_) {
        flushSummary(*metrics->Add(), histogram.get(), snapshot_time_ms);
      }
      if (emit_histogram_) {
        flushHistogram(*metrics->Add(), histogram.get(), snapshot_time_ms);
      }
    }
  }

  return metrics;
}

void MetricsFlusher::flushCounter(io::prometheus::client::MetricFamily& metrics_family,
                                  const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot,
                                  int64_t snapshot_time_ms) const {
  auto* metric = populateMetricsFamily(metrics_family, io::prometheus::client::MetricType::COUNTER,
                                       snapshot_time_ms, counter_snapshot.counter_.get());
  auto* counter_metric = metric->mutable_counter();
  if (report_counters_as_deltas_) {
    counter_metric->set_value(counter_snapshot.delta_);
  } else {
    counter_metric->set_value(counter_snapshot.counter_.get().value());
  }
}

void MetricsFlusher::flushGauge(io::prometheus::client::MetricFamily& metrics_family,
                                const Stats::Gauge& gauge, int64_t snapshot_time_ms) const {
  auto* metric = populateMetricsFamily(metrics_family, io::prometheus::client::MetricType::GAUGE,
                                       snapshot_time_ms, gauge);
  auto* gauge_metric = metric->mutable_gauge();
  gauge_metric->set_value(gauge.value());
}

void MetricsFlusher::flushHistogram(io::prometheus::client::MetricFamily& metrics_family,
                                    const Stats::ParentHistogram& envoy_histogram,
                                    int64_t snapshot_time_ms) const {

  const Stats::HistogramStatistics& hist_stats = envoy_histogram.intervalStatistics();
  auto* histogram_metric =
      populateMetricsFamily(metrics_family, io::prometheus::client::MetricType::HISTOGRAM,
                            snapshot_time_ms, envoy_histogram);
  auto* histogram = histogram_metric->mutable_histogram();
  histogram->set_sample_count(hist_stats.sampleCount());
  histogram->set_sample_sum(hist_stats.sampleSum());
  for (size_t i = 0; i < hist_stats.supportedBuckets().size(); i++) {
    auto* bucket = histogram->add_bucket();
    bucket->set_upper_bound(hist_stats.supportedBuckets()[i]);
    bucket->set_cumulative_count(hist_stats.computedBuckets()[i]);
  }
}

void MetricsFlusher::flushSummary(io::prometheus::client::MetricFamily& metrics_family,
                                  const Stats::ParentHistogram& envoy_histogram,
                                  int64_t snapshot_time_ms) const {

  const Stats::HistogramStatistics& hist_stats = envoy_histogram.intervalStatistics();
  auto* summary_metric =
      populateMetricsFamily(metrics_family, io::prometheus::client::MetricType::SUMMARY,
                            snapshot_time_ms, envoy_histogram);
  auto* summary = summary_metric->mutable_summary();
  for (size_t i = 0; i < hist_stats.supportedQuantiles().size(); i++) {
    auto* quantile = summary->add_quantile();
    quantile->set_quantile(hist_stats.supportedQuantiles()[i]);
    quantile->set_value(hist_stats.computedQuantiles()[i]);
  }
  summary->set_sample_count(hist_stats.sampleCount());
  summary->set_sample_sum(hist_stats.sampleSum());
}

io::prometheus::client::Metric*
MetricsFlusher::populateMetricsFamily(io::prometheus::client::MetricFamily& metrics_family,
                                      io::prometheus::client::MetricType type,
                                      int64_t snapshot_time_ms, const Stats::Metric& metric) const {
  metrics_family.set_type(type);
  auto* prometheus_metric = metrics_family.add_metric();
  prometheus_metric->set_timestamp_ms(snapshot_time_ms);

  if (emit_labels_) {
    // TODO(snowp): Look into the perf implication of this. We need to take a lock on the symbol
    // table to stringify the StatNames, which could result in some lock contention. Consider
    // caching the conversion between stat handle to extracted tags.
    metrics_family.set_name(metric.tagExtractedName());
    for (const auto& tag : metric.tags()) {
      auto* label = prometheus_metric->add_label();
      label->set_name(tag.name_);
      label->set_value(tag.value_);
    }
  } else {
    metrics_family.set_name(metric.name());
  }

  return prometheus_metric;
}
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
