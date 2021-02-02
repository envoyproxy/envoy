#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/metrics/v3/metrics_service.pb.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

GrpcMetricsStreamerImpl::GrpcMetricsStreamerImpl(
    Grpc::AsyncClientFactoryPtr&& factory, const LocalInfo::LocalInfo& local_info,
    envoy::config::core::v3::ApiVersion transport_api_version)
    : GrpcMetricsStreamer<envoy::service::metrics::v3::StreamMetricsMessage,
                          envoy::service::metrics::v3::StreamMetricsResponse>(*factory),
      local_info_(local_info),
      service_method_(
          Grpc::VersionedMethods("envoy.service.metrics.v3.MetricsService.StreamMetrics",
                                 "envoy.service.metrics.v2.MetricsService.StreamMetrics")
              .getMethodDescriptorForVersion(transport_api_version)),
      transport_api_version_(transport_api_version) {}

void GrpcMetricsStreamerImpl::send(MetricsPtr&& metrics) {
  envoy::service::metrics::v3::StreamMetricsMessage message;
  message.mutable_envoy_metrics()->Reserve(metrics->size());
  message.mutable_envoy_metrics()->MergeFrom(*metrics);

  if (stream_ == nullptr) {
    stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
    // For perf reasons, the identifier is only sent on establishing the stream.
    auto* identifier = message.mutable_identifier();
    *identifier->mutable_node() = local_info_.node();
  }
  if (stream_ != nullptr) {
    stream_->sendMessage(message, transport_api_version_, false);
  }
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
    if (counter.counter_.get().used()) {
      flushCounter(*metrics->Add(), counter, snapshot_time_ms);
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      flushGauge(*metrics->Add(), gauge.get(), snapshot_time_ms);
    }
  }

  for (const auto& histogram : snapshot.histograms()) {
    if (histogram.get().used()) {
      flushHistogram(*metrics->Add(), *metrics->Add(), histogram.get(), snapshot_time_ms);
    }
  }

  return metrics;
}

void MetricsFlusher::flushCounter(io::prometheus::client::MetricFamily& metrics_family,
                                  const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot,
                                  int64_t snapshot_time_ms) const {
  metrics_family.set_type(io::prometheus::client::MetricType::COUNTER);
  metrics_family.set_name(counter_snapshot.counter_.get().name());
  auto* metric = metrics_family.add_metric();
  metric->set_timestamp_ms(snapshot_time_ms);
  auto* counter_metric = metric->mutable_counter();
  if (report_counters_as_deltas_) {
    counter_metric->set_value(counter_snapshot.delta_);
  } else {
    counter_metric->set_value(counter_snapshot.counter_.get().value());
  }
}

void MetricsFlusher::flushGauge(io::prometheus::client::MetricFamily& metrics_family,
                                const Stats::Gauge& gauge, int64_t snapshot_time_ms) const {
  metrics_family.set_type(io::prometheus::client::MetricType::GAUGE);
  metrics_family.set_name(gauge.name());
  auto* metric = metrics_family.add_metric();
  metric->set_timestamp_ms(snapshot_time_ms);
  auto* gauge_metric = metric->mutable_gauge();
  gauge_metric->set_value(gauge.value());
}

void MetricsFlusher::flushHistogram(io::prometheus::client::MetricFamily& summary_metrics_family,
                                    io::prometheus::client::MetricFamily& histogram_metrics_family,
                                    const Stats::ParentHistogram& envoy_histogram,
                                    int64_t snapshot_time_ms) const {
  // TODO(ramaraochavali): Currently we are sending both quantile information and bucket
  // information. We should make this configurable if it turns out that sending both affects
  // performance.

  // Add summary information for histograms.
  summary_metrics_family.set_type(io::prometheus::client::MetricType::SUMMARY);
  summary_metrics_family.set_name(envoy_histogram.name());
  auto* summary_metric = summary_metrics_family.add_metric();
  summary_metric->set_timestamp_ms(snapshot_time_ms);
  auto* summary = summary_metric->mutable_summary();
  const Stats::HistogramStatistics& hist_stats = envoy_histogram.intervalStatistics();
  for (size_t i = 0; i < hist_stats.supportedQuantiles().size(); i++) {
    auto* quantile = summary->add_quantile();
    quantile->set_quantile(hist_stats.supportedQuantiles()[i]);
    quantile->set_value(hist_stats.computedQuantiles()[i]);
  }

  // Add bucket information for histograms.
  histogram_metrics_family.set_type(io::prometheus::client::MetricType::HISTOGRAM);
  histogram_metrics_family.set_name(envoy_histogram.name());
  auto* histogram_metric = histogram_metrics_family.add_metric();
  histogram_metric->set_timestamp_ms(snapshot_time_ms);
  auto* histogram = histogram_metric->mutable_histogram();
  histogram->set_sample_count(hist_stats.sampleCount());
  histogram->set_sample_sum(hist_stats.sampleSum());
  for (size_t i = 0; i < hist_stats.supportedBuckets().size(); i++) {
    auto* bucket = histogram->add_bucket();
    bucket->set_upper_bound(hist_stats.supportedBuckets()[i]);
    bucket->set_cumulative_count(hist_stats.computedBuckets()[i]);
  }
}
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
