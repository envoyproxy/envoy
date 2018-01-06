#include "common/stats/grpc_metrics_service_impl.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"

#include "fmt/format.h"

namespace Envoy {
namespace Stats {
namespace Metrics {

GrpcMetricsStreamerImpl::GrpcMetricsStreamerImpl(GrpcMetricsServiceClientFactoryPtr&& factory,
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
  auto it = parent_.stream_map_.find("metrics");
  ASSERT(it != parent_.stream_map_.end());
  if (it->second.stream_ != nullptr) {
    // Only erase if we have a stream. Otherwise we had an inline failure and we will clear the
    // stream data in send().
    parent_.stream_map_.erase(it);
  }
}

GrpcMetricsStreamerImpl::ThreadLocalStreamer::ThreadLocalStreamer(
    const SharedStateSharedPtr& shared_state)
    : client_(shared_state->factory_->create()), shared_state_(shared_state) {}

void GrpcMetricsStreamerImpl::ThreadLocalStreamer::send(
    envoy::api::v2::StreamMetricsMessage& message) {
  auto stream_it = stream_map_.find("metrics");
  if (stream_it == stream_map_.end()) {
    stream_it = stream_map_.emplace("metrics", ThreadLocalStream(*this)).first;
  }

  auto& stream_entry = stream_it->second;
  if (stream_entry.stream_ == nullptr) {
    stream_entry.stream_ =
        client_->start(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                           "envoy.api.v2.MetricsService.StreamMetrics"),
                       stream_entry);
  }

  if (stream_entry.stream_ != nullptr) {
    stream_entry.stream_->sendMessage(message, false);
  } else {
    stream_map_.erase(stream_it);
  }
}

MetricsServiceSink::MetricsServiceSink(const LocalInfo::LocalInfo& local_info,
                                       const std::string& cluster_name,
                                       ThreadLocal::SlotAllocator& tls,
                                       Upstream::ClusterManager& cluster_manager,
                                       Stats::Scope& scope,
                                       GrpcMetricsStreamerSharedPtr grpc_metrics_streamer)
    : tls_(tls.allocateSlot()), cluster_manager_(cluster_manager),
      cx_overflow_stat_(scope.counter("statsd.cx_overflow")) {
  grpc_metrics_streamer_ = grpc_metrics_streamer;
  Config::Utility::checkClusterAndLocalInfo("metrics service", cluster_name, cluster_manager,
                                            local_info);
  cluster_info_ = cluster_manager.get(cluster_name)->info();
}
} // namespace Metrics
} // namespace Stats
} // namespace Envoy
