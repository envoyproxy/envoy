#include "source/extensions/tracers/skywalking/trace_segment_reporter.h"

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authentication_handle(Http::CustomHeaders::get().Authentication);

} // namespace

TraceSegmentReporter::TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                           Event::Dispatcher& dispatcher,
                                           Random::RandomGenerator& random_generator,
                                           SkyWalkingTracerStatsSharedPtr stats,
                                           uint32_t delayed_buffer_size, const std::string& token)
    : tracing_stats_(stats), client_(factory->createUncachedRawAsyncClient()),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "skywalking.v3.TraceSegmentReportService.collect")),
      random_generator_(random_generator), token_(token),
      delayed_buffer_size_(delayed_buffer_size) {

  static constexpr uint32_t RetryInitialDelayMs = 500;
  static constexpr uint32_t RetryMaxDelayMs = 30000;
  backoff_strategy_ = std::make_unique<JitteredExponentialBackOffStrategy>(
      RetryInitialDelayMs, RetryMaxDelayMs, random_generator_);

  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  establishNewStream();
}

TraceSegmentReporter::~TraceSegmentReporter() { closeStream(); }

void TraceSegmentReporter::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  if (!token_.empty()) {
    metadata.setInline(authentication_handle.handle(), token_);
  }
}

void TraceSegmentReporter::report(TracingContextPtr tracing_context) {
  ASSERT(tracing_context);
  auto request = tracing_context->createSegmentObject();
  ENVOY_LOG(trace, "Try to report segment to SkyWalking Server:\n{}", request.DebugString());

  if (stream_ != nullptr) {
    tracing_stats_->segments_sent_.inc();
    stream_->sendMessage(request, false);
    return;
  }
  // Null stream_ and cache segment data temporarily.
  delayed_segments_cache_.emplace(request);
  if (delayed_segments_cache_.size() > delayed_buffer_size_) {
    tracing_stats_->segments_dropped_.inc();
    delayed_segments_cache_.pop();
  }
}

void TraceSegmentReporter::flushTraceSegments() {
  ENVOY_LOG(debug, "Flush segments in cache to SkyWalking backend service");
  while (!delayed_segments_cache_.empty() && stream_ != nullptr) {
    tracing_stats_->segments_sent_.inc();
    tracing_stats_->segments_flushed_.inc();
    stream_->sendMessage(delayed_segments_cache_.front(), false);
    delayed_segments_cache_.pop();
  }
  tracing_stats_->cache_flushed_.inc();
}

void TraceSegmentReporter::closeStream() {
  if (stream_ != nullptr) {
    flushTraceSegments();
    stream_->closeStream();
  }
}

void TraceSegmentReporter::onRemoteClose(Grpc::Status::GrpcStatus status,
                                         const std::string& message) {
  ENVOY_LOG(debug, "{} gRPC stream closed: {}, {}", service_method_.name(), status, message);
  stream_ = nullptr;
  handleFailure();
}

void TraceSegmentReporter::establishNewStream() {
  ENVOY_LOG(debug, "Try to create new {} gRPC stream for reporter", service_method_.name());
  stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "Failed to create {} gRPC stream", service_method_.name());
    return;
  }
  // TODO(wbpcode): Even if stream_ is not empty, there is no guarantee that the connection will be
  // established correctly. If there is a connection failure, the onRemoteClose method will be
  // called. Currently, we lack a way to determine whether the connection is truly available. This
  // may cause partial data loss.
  if (!delayed_segments_cache_.empty()) {
    flushTraceSegments();
  }
  backoff_strategy_->reset();
}

void TraceSegmentReporter::handleFailure() { setRetryTimer(); }

void TraceSegmentReporter::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
