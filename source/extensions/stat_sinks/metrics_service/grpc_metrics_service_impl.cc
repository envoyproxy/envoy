#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/source.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

GrpcMetricsStreamerImpl::GrpcMetricsStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory,
                                                 const LocalInfo::LocalInfo& local_info)
    : client_(factory->create()), local_info_(local_info) {}

void GrpcMetricsStreamerImpl::send(envoy::service::metrics::v2::StreamMetricsMessage& message) {
  if (stream_ == nullptr) {
    stream_ = client_->start(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                                 "envoy.service.metrics.v2.MetricsService.StreamMetrics"),
                             *this);
    auto* identifier = message.mutable_identifier();
    *identifier->mutable_node() = local_info_.node();
  }
  if (stream_ != nullptr) {
    stream_->sendMessage(message, false);
  }
}

MetricsServiceSink::MetricsServiceSink(const GrpcMetricsStreamerSharedPtr& grpc_metrics_streamer,
                                       Event::TimeSystem& time_system)
    : grpc_metrics_streamer_(grpc_metrics_streamer), time_system_(time_system) {}

void MetricsServiceSink::flushCounter(const Stats::Counter& counter) {
  io::prometheus::client::MetricFamily* metrics_family = message_.add_envoy_metrics();
  metrics_family->set_type(io::prometheus::client::MetricType::COUNTER);
  metrics_family->set_name(counter.name());
  auto* metric = metrics_family->add_metric();
  metric->set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                               time_system_.systemTime().time_since_epoch())
                               .count());
  auto* counter_metric = metric->mutable_counter();
  counter_metric->set_value(counter.value());
}

void MetricsServiceSink::flushGauge(const Stats::Gauge& gauge) {
  io::prometheus::client::MetricFamily* metrics_family = message_.add_envoy_metrics();
  metrics_family->set_type(io::prometheus::client::MetricType::GAUGE);
  metrics_family->set_name(gauge.name());
  auto* metric = metrics_family->add_metric();
  metric->set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                               time_system_.systemTime().time_since_epoch())
                               .count());
  auto* gauage_metric = metric->mutable_gauge();
  gauage_metric->set_value(gauge.value());
}
void MetricsServiceSink::flushHistogram(const Stats::ParentHistogram& histogram) {
  io::prometheus::client::MetricFamily* metrics_family = message_.add_envoy_metrics();
  metrics_family->set_type(io::prometheus::client::MetricType::SUMMARY);
  metrics_family->set_name(histogram.name());
  auto* metric = metrics_family->add_metric();
  metric->set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                               time_system_.systemTime().time_since_epoch())
                               .count());
  auto* summary_metric = metric->mutable_summary();
  const Stats::HistogramStatistics& hist_stats = histogram.intervalStatistics();
  for (size_t i = 0; i < hist_stats.supportedQuantiles().size(); i++) {
    auto* quantile = summary_metric->add_quantile();
    quantile->set_quantile(hist_stats.supportedQuantiles()[i]);
    quantile->set_value(hist_stats.computedQuantiles()[i]);
  }
}

void MetricsServiceSink::flush(Stats::Source& source) {
  message_.clear_envoy_metrics();
  const std::vector<Stats::CounterSharedPtr>& counters = source.cachedCounters();
  const std::vector<Stats::GaugeSharedPtr>& gauges = source.cachedGauges();
  const std::vector<Stats::ParentHistogramSharedPtr>& histograms = source.cachedHistograms();
  // TODO(mrice32): there's probably some more sophisticated preallocation we can do here where we
  // actually preallocate the submessages and then pass ownership to the proto (rather than just
  // preallocating the pointer array).
  message_.mutable_envoy_metrics()->Reserve(counters.size() + gauges.size() + histograms.size());
  for (const Stats::CounterSharedPtr& counter : counters) {
    if (counter->used()) {
      flushCounter(*counter);
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : gauges) {
    if (gauge->used()) {
      flushGauge(*gauge);
    }
  }

  for (const Stats::ParentHistogramSharedPtr& histogram : histograms) {
    if (histogram->used()) {
      flushHistogram(*histogram);
    }
  }

  grpc_metrics_streamer_->send(message_);
  // for perf reasons, clear the identifer after the first flush.
  if (message_.has_identifier()) {
    message_.clear_identifier();
  }
}

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
