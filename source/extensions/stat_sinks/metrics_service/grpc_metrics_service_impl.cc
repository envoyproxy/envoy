#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

GrpcMetricsStreamerImpl::GrpcMetricsStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory,
                                                 ThreadLocal::SlotAllocator& tls,
                                                 const LocalInfo::LocalInfo& local_info)
    : tls_slot_(tls.allocateSlot()) {
  SharedStateSharedPtr shared_state = std::make_shared<SharedState>(std::move(factory), local_info);
  tls_slot_->set([shared_state](Event::Dispatcher&) {
    return ThreadLocal::ThreadLocalObjectSharedPtr{new ThreadLocalStreamer(shared_state)};
  });
}

void GrpcMetricsStreamerImpl::ThreadLocalStream::onRemoteClose(Grpc::Status::GrpcStatus,
                                                               const std::string&) {
  // Only erase if we have a stream. Otherwise we had an inline failure and we will clear the
  // stream data in send().
  if (parent_.thread_local_stream_->stream_ != nullptr) {
    parent_.thread_local_stream_ = nullptr;
  }
}

GrpcMetricsStreamerImpl::ThreadLocalStreamer::ThreadLocalStreamer(
    const SharedStateSharedPtr& shared_state)
    : client_(shared_state->factory_->create()), shared_state_(shared_state) {}

void GrpcMetricsStreamerImpl::ThreadLocalStreamer::send(
    envoy::service::metrics::v2::StreamMetricsMessage& message) {
  if (thread_local_stream_ == nullptr) {
    thread_local_stream_ = std::make_shared<ThreadLocalStream>(*this);
  }

  if (thread_local_stream_->stream_ == nullptr) {
    thread_local_stream_->stream_ =
        client_->start(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                           "envoy.service.metrics.v2.MetricsService.StreamMetrics"),
                       *thread_local_stream_);
    auto* identifier = message.mutable_identifier();
    *identifier->mutable_node() = shared_state_->local_info_.node();
  }

  if (thread_local_stream_->stream_ != nullptr) {
    thread_local_stream_->stream_->sendMessage(message, false);
  } else {
    thread_local_stream_ = nullptr;
  }
}

MetricsServiceSink::MetricsServiceSink(const GrpcMetricsStreamerSharedPtr& grpc_metrics_streamer)
    : grpc_metrics_streamer_(grpc_metrics_streamer) {}

void MetricsServiceSink::flushCounter(const Stats::Counter& counter, uint64_t) {
  io::prometheus::client::MetricFamily* metrics_family = message_.add_envoy_metrics();
  metrics_family->set_type(io::prometheus::client::MetricType::COUNTER);
  metrics_family->set_name(counter.name());
  auto* metric = metrics_family->add_metric();
  metric->set_timestamp_ms(std::chrono::system_clock::now().time_since_epoch().count());
  auto* counter_metric = metric->mutable_counter();
  counter_metric->set_value(counter.value());
}

void MetricsServiceSink::flushGauge(const Stats::Gauge& gauge, uint64_t value) {
  io::prometheus::client::MetricFamily* metrics_family = message_.add_envoy_metrics();
  metrics_family->set_type(io::prometheus::client::MetricType::GAUGE);
  metrics_family->set_name(gauge.name());
  auto* metric = metrics_family->add_metric();
  metric->set_timestamp_ms(std::chrono::system_clock::now().time_since_epoch().count());
  auto* gauage_metric = metric->mutable_gauge();
  gauage_metric->set_value(value);
}
void MetricsServiceSink::flushHistogram(const Stats::ParentHistogram& histogram) {
  io::prometheus::client::MetricFamily* metrics_family = message_.add_envoy_metrics();
  metrics_family->set_type(io::prometheus::client::MetricType::SUMMARY);
  metrics_family->set_name(histogram.name());
  auto* metric = metrics_family->add_metric();
  metric->set_timestamp_ms(std::chrono::system_clock::now().time_since_epoch().count());
  auto* summary_metric = metric->mutable_summary();
  const Stats::HistogramStatistics& hist_stats = histogram.intervalStatistics();
  for (size_t i = 0; i < hist_stats.supportedQuantiles().size(); i++) {
    auto* quantile = summary_metric->add_quantile();
    quantile->set_quantile(hist_stats.supportedQuantiles()[i]);
    quantile->set_value(hist_stats.computedQuantiles()[i]);
  }
}

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
