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

namespace Envoy {
namespace Stats {
namespace Metrics {

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
} // namespace Metrics
} // namespace Stats
} // namespace Envoy
