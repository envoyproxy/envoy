#include "source/common/http/on_demand_async_client.h"

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/linked_object.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

SINGLETON_MANAGER_REGISTRATION(on_demand_async_client_provider);

OnDemandAsyncClientProvider::OnDemandAsyncClientProvider(Upstream::ClusterManager& cm,
                                                         ThreadLocal::Instance& tls)
    : cm_(cm), tls_(tls), clients_(tls) {
  // Do not capture the provider: initialization can run after its destruction.
  clients_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalClient>(); });
}

std::shared_ptr<OnDemandAsyncClientProvider>
OnDemandAsyncClientProvider::getOrCreate(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls,
                                         Singleton::Manager& manager) {
  return manager.getTyped<OnDemandAsyncClientProvider>(
      SINGLETON_MANAGER_REGISTERED_NAME(on_demand_async_client_provider), [&cm, &tls] {
        return std::shared_ptr<OnDemandAsyncClientProvider>(
            new OnDemandAsyncClientProvider(cm, tls));
      });
}

OnDemandAsyncClient& OnDemandAsyncClientProvider::get() {
  // Only this worker accesses its holder, including during lazy initialization.
  auto& client = clients_->client_;
  if (client == nullptr) {
    client = std::make_unique<OnDemandAsyncClient>(cm_, tls_.dispatcher());
  }
  return *client;
}

class OnDemandAsyncClient::Request : public AsyncClient::OngoingRequest,
                                     private AsyncClient::Callbacks,
                                     private SidestreamWatermarkCallbacks,
                                     public Event::DeferredDeletable,
                                     public LinkedObject<Request> {
public:
  Request(OnDemandAsyncClient& parent, std::string cluster, RequestHeaderMapPtr&& headers,
          AsyncClient::Callbacks& callbacks, const AsyncClient::RequestOptions& options,
          const Upstream::OdCdsContextSharedPtr& discovery_context, bool end_stream,
          DiscoveryCallback discovery_callback)
      : parent_(parent), cluster_(std::move(cluster)), headers_(std::move(headers)),
        callbacks_(callbacks), options_(options), discovery_context_(discovery_context),
        discovery_callback_(std::move(discovery_callback)), body_(options.account_),
        stream_info_(parent.dispatcher_.timeSource(), nullptr,
                     StreamInfo::FilterState::LifeSpan::FilterChain),
        started_at_(parent.dispatcher_.timeSource().monotonicTime()), end_stream_(end_stream) {
    ASSERT(discovery_context_ != nullptr);
    // Never retain pointers into the downstream filter chain while waiting for discovery.
    if (options_.parent_context.stream_info != nullptr) {
      stream_info_.route_ = options_.parent_context.stream_info->routeSharedPtr();
      stream_info_.dynamicMetadata().CopyFrom(
          options_.parent_context.stream_info->dynamicMetadata());
      options_.parent_context.stream_info = &stream_info_;
    }
    const std::string span_name = options_.child_span_name_.empty()
                                      ? "async " + cluster_ + " egress"
                                      : options_.child_span_name_;
    span_ = options_.parent_span_ == nullptr
                ? std::make_unique<Tracing::NullSpan>()
                : options_.parent_span_->spawnChild(Tracing::EgressConfig::get(), span_name,
                                                    parent.dispatcher_.timeSource().systemTime());
    if (options_.sampled_.has_value()) {
      span_->setSampled(*options_.sampled_);
    }
    span_->setTag(Tracing::Tags::get().UpstreamCluster, cluster_);
    // The mirror span owns its tracing context; the downstream span may be destroyed while waiting.
    options_.parent_span_ = nullptr;
    options_.sidestream_watermark_callbacks = nullptr;
  }

  ~Request() override { finishSpan(); }

  // AsyncClient::OngoingRequest
  void sendHeaders(RequestHeaderMap&, bool) override {
    IS_ENVOY_BUG("headers already supplied");
    reset();
  }
  void sendData(Buffer::Instance& data, bool end_stream) override {
    if (finished_) {
      return;
    }
    end_stream_ = end_stream;
    if (upstream_ != nullptr) {
      upstream_->sendData(data, end_stream);
    } else {
      const uint64_t limit = options_.buffer_limit_.value_or(0);
      // Match async-client zero/unset semantics. Check before retaining the caller's data,
      // including a final chunk, and use subtraction to avoid overflowing the byte count.
      if (limit != 0 && (body_.length() > limit || data.length() > limit - body_.length())) {
        terminate(true, DiscoveryResult::BufferOverflow);
        return;
      }
      body_.move(data);
    }
  }
  void sendTrailers(RequestTrailerMap& trailers) override {
    captureAndSendTrailers(createHeaderMap<RequestTrailerMapImpl>(trailers));
  }
  void captureAndSendTrailers(RequestTrailerMapPtr&& trailers) override {
    if (finished_) {
      return;
    }
    end_stream_ = true;
    if (upstream_ != nullptr) {
      upstream_->captureAndSendTrailers(std::move(trailers));
    } else {
      trailers_ = std::move(trailers);
    }
  }
  OptRef<AsyncClient::RequestContext> context() const override {
    return makeOptRefFromPtr(options_.context_.get());
  }
  void cancel() override { terminate(true); }
  void reset() override { terminate(false); }
  void setDestructorCallback(AsyncClient::StreamDestructorCallbacks callback) override {
    ASSERT(!destructor_callback_);
    destructor_callback_ = std::move(callback);
  }
  void removeDestructorCallback() override { destructor_callback_ = nullptr; }
  void setWatermarkCallbacks(SidestreamWatermarkCallbacks& callbacks) override {
    ASSERT(watermark_callbacks_ == nullptr);
    watermark_callbacks_ = &callbacks;
    for (uint32_t i = 0; i < high_watermarks_; ++i) {
      callbacks.onSidestreamAboveHighWatermark();
    }
  }
  void removeWatermarkCallbacks() override {
    if (watermark_callbacks_ != nullptr) {
      for (uint32_t i = 0; i < high_watermarks_; ++i) {
        watermark_callbacks_->onSidestreamBelowLowWatermark();
      }
      watermark_callbacks_ = nullptr;
    }
  }
  bool isAboveWriteBufferHighWatermark() const override { return high_watermarks_ != 0; }
  const StreamInfo::StreamInfo& streamInfo() const override {
    if (upstream_ != nullptr) {
      return upstream_->streamInfo();
    }
    return stream_info_;
  }
  StreamInfo::StreamInfo& streamInfo() override {
    if (upstream_ != nullptr) {
      return upstream_->streamInfo();
    }
    return stream_info_;
  }

private:
  friend class OnDemandAsyncClient;

  void start() {
    timer_ = parent_.dispatcher_.createTimer(
        [this] { discovered(Upstream::ClusterDiscoveryStatus::Timeout); });
    timer_->enableTimer(discovery_context_->timeout());
    auto handle = discovery_context_->api().requestOnDemandClusterDiscovery(
        cluster_,
        std::make_unique<Upstream::ClusterDiscoveryCallback>(
            [this](Upstream::ClusterDiscoveryStatus status) { discovered(status); }),
        discovery_context_->timeout());
    // Also tolerate a synchronous completion without installing a stale cancellation handle.
    if (waiting_) {
      discovery_ = std::move(handle);
    }
  }

  bool finished() const { return finished_; }

  void discovered(Upstream::ClusterDiscoveryStatus status) {
    if (!waiting_ || finished_) {
      return;
    }
    auto* cluster = status == Upstream::ClusterDiscoveryStatus::Available
                        ? parent_.cm_.getThreadLocalCluster(cluster_)
                        : nullptr;
    if (status == Upstream::ClusterDiscoveryStatus::Available && cluster == nullptr) {
      // A warmed cluster may have been removed before this worker's callback ran.
      status = Upstream::ClusterDiscoveryStatus::Missing;
    }
    switch (status) {
    case Upstream::ClusterDiscoveryStatus::Available:
      stopWaiting(DiscoveryResult::Available);
      break;
    case Upstream::ClusterDiscoveryStatus::Missing:
      stopWaiting(DiscoveryResult::Missing);
      break;
    case Upstream::ClusterDiscoveryStatus::Timeout:
      stopWaiting(DiscoveryResult::Timeout);
      break;
    }
    if (cluster == nullptr) {
      finalizeWithoutUpstream();
      callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
      finish();
      return;
    }
    if (end_stream_ && trailers_ == nullptr) {
      // Match the normal complete-request path. In particular, send() ends a header-only
      // request at headers; startRequest() followed by empty DATA would force HTTP/1 chunking
      // and reset the request if asynchronous host selection is still pending.
      buffered_request_ = true;
      auto message = std::make_unique<RequestMessageImpl>(std::move(headers_));
      message->body().move(body_);
      buffered_upstream_ =
          cluster->httpAsyncClient().send(std::move(message), *this, options_, std::move(span_));
      if (buffered_upstream_ == nullptr) {
        if (!finalized_) {
          finalizeWithoutUpstream();
          callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
        }
        finish();
      }
      return;
    }
    // send() does not support request trailers, so retain the streaming path for them too.
    upstream_ = cluster->httpAsyncClient().startRequest(std::move(headers_), *this, options_,
                                                        std::move(span_));
    if (upstream_ == nullptr) {
      if (!finalized_) {
        finalizeWithoutUpstream();
        callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
      }
      finish();
      return;
    }
    upstream_->setDestructorCallback([this] {
      upstream_ = nullptr;
      finish();
    });
    upstream_->setWatermarkCallbacks(*this);
    // Transfer retained bytes immediately, even for an incomplete upload. The ordinary
    // async client owns buffering and watermark backpressure after discovery succeeds.
    if (body_.length() != 0) {
      upstream_->sendData(body_, false);
      body_.drain(body_.length());
    }
    if (upstream_ != nullptr && trailers_ != nullptr) {
      upstream_->captureAndSendTrailers(std::move(trailers_));
    }
  }

  void terminate(bool cancelled, DiscoveryResult result = DiscoveryResult::Cancelled) {
    if (finished_) {
      return;
    }
    stopWaiting(result);
    if (upstream_ != nullptr) {
      // cancel() finalizes callbacks synchronously. Queue the delegate's deletion before ours,
      // so its retained parent stream-info snapshot remains valid during destruction.
      auto* upstream = upstream_;
      upstream_ = nullptr;
      upstream->removeDestructorCallback();
      upstream->removeWatermarkCallbacks();
      if (cancelled) {
        upstream->cancel();
      } else {
        upstream->reset();
      }
    } else if (buffered_upstream_ != nullptr) {
      auto* upstream = buffered_upstream_;
      buffered_upstream_ = nullptr;
      // A complete request exposes cancel(), but not reset(). Cancel the delegate and report
      // a reset ourselves when requested; cancel() still finalizes the upstream span.
      upstream->cancel();
      if (!cancelled) {
        callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
      }
    } else {
      finalizeWithoutUpstream();
      if (!cancelled) {
        callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
      }
    }
    finish();
  }

  // AsyncClient::Callbacks
  void onSuccess(const AsyncClient::Request&, ResponseMessagePtr&& response) override {
    buffered_upstream_ = nullptr;
    callbacks_.onSuccess(*this, std::move(response));
    if (buffered_request_) {
      finish();
    }
  }
  void onFailure(const AsyncClient::Request&, AsyncClient::FailureReason reason) override {
    buffered_upstream_ = nullptr;
    callbacks_.onFailure(*this, reason);
    if (buffered_request_) {
      finish();
    }
  }
  void onBeforeFinalizeUpstreamSpan(Tracing::Span& span,
                                    const ResponseHeaderMap* headers) override {
    finalize(span, headers);
  }

  // SidestreamWatermarkCallbacks
  // Like AsyncStreamImpl, this adapter does not subscribe to downstream response watermarks.
  void addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void onSidestreamAboveHighWatermark() override {
    ++high_watermarks_;
    if (watermark_callbacks_ != nullptr) {
      watermark_callbacks_->onSidestreamAboveHighWatermark();
    }
  }
  void onSidestreamBelowLowWatermark() override {
    ASSERT(high_watermarks_ > 0);
    --high_watermarks_;
    if (watermark_callbacks_ != nullptr) {
      watermark_callbacks_->onSidestreamBelowLowWatermark();
    }
  }

  void finalize(Tracing::Span& span, const ResponseHeaderMap* headers) {
    if (!finalized_) {
      finalized_ = true;
      callbacks_.onBeforeFinalizeUpstreamSpan(span, headers);
    }
  }
  void finalizeWithoutUpstream() {
    // A delegate that fails synchronously may already have consumed and finalized the owned span.
    Tracing::NullSpan null_span;
    finalize(span_ != nullptr ? *span_ : null_span, nullptr);
  }
  void finishSpan() {
    if (span_ != nullptr) {
      span_->finishSpan();
      span_.reset();
    }
  }
  void stopWaiting(DiscoveryResult result) {
    if (waiting_) {
      waiting_ = false;
      discovery_.reset();
      if (timer_ != nullptr) {
        timer_->disableTimer();
      }
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          parent_.dispatcher_.timeSource().monotonicTime() - started_at_);
      absl::string_view outcome;
      switch (result) {
      case DiscoveryResult::Available:
        outcome = "available";
        break;
      case DiscoveryResult::Missing:
        outcome = "missing";
        break;
      case DiscoveryResult::Timeout:
        outcome = "timeout";
        break;
      case DiscoveryResult::Cancelled:
        outcome = "cancelled";
        break;
      case DiscoveryResult::BufferOverflow:
        outcome = "buffer_overflow";
        break;
      }
      span_->setTag("discovery.result", outcome);
      if (result == DiscoveryResult::Missing || result == DiscoveryResult::Timeout) {
        span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
      } else if (result == DiscoveryResult::Cancelled) {
        span_->setTag(Tracing::Tags::get().Canceled, Tracing::Tags::get().True);
      }
      discovery_callback_(result, elapsed);
    }
  }
  void finish() {
    if (finished_) {
      return;
    }
    finished_ = true;
    removeWatermarkCallbacks();
    body_.drain(body_.length());
    finishSpan();
    if (destructor_callback_) {
      auto callback = std::move(destructor_callback_);
      callback();
    }
    auto request = removeFromList(parent_.requests_);
    if (buffered_request_) {
      // send() has no destructor callback, and its completion callbacks run before it queues
      // its own deletion. Post ours so the parent stream-info snapshot outlives the delegate.
      // DispatcherImpl drains deferred deletes before post callbacks; see
      // DeferredDeleteTest.DeferredDeleteAndPostOrdering and the real-dispatcher coverage in
      // on_demand_async_client_lifetime_test.cc (including shutdown with queued cleanup).
      auto& dispatcher = parent_.dispatcher_;
      dispatcher.post([&dispatcher, request = std::move(request)]() mutable {
        dispatcher.deferredDelete(std::move(request));
      });
    } else {
      parent_.dispatcher_.deferredDelete(std::move(request));
    }
  }

  OnDemandAsyncClient& parent_;
  const std::string cluster_;
  RequestHeaderMapPtr headers_;
  AsyncClient::Callbacks& callbacks_;
  AsyncClient::RequestOptions options_;
  const Upstream::OdCdsContextSharedPtr discovery_context_;
  DiscoveryCallback discovery_callback_;
  Buffer::OwnedImpl body_;
  RequestTrailerMapPtr trailers_;
  StreamInfo::StreamInfoImpl stream_info_;
  Tracing::SpanPtr span_;
  const MonotonicTime started_at_;
  Upstream::ClusterDiscoveryCallbackHandlePtr discovery_;
  Event::TimerPtr timer_;
  AsyncClient::OngoingRequest* upstream_{};
  AsyncClient::Request* buffered_upstream_{};
  AsyncClient::StreamDestructorCallbacks destructor_callback_;
  SidestreamWatermarkCallbacks* watermark_callbacks_{};
  uint32_t high_watermarks_{};
  bool end_stream_;
  bool waiting_{true};
  bool finished_{};
  bool finalized_{};
  bool buffered_request_{};
};

OnDemandAsyncClient::OnDemandAsyncClient(Upstream::ClusterManager& cm,
                                         Event::Dispatcher& dispatcher)
    : cm_(cm), dispatcher_(dispatcher) {}

OnDemandAsyncClient::~OnDemandAsyncClient() {
  while (!requests_.empty()) {
    requests_.front()->cancel();
  }
}

AsyncClient::OngoingRequest*
OnDemandAsyncClient::startRequest(const std::string& cluster, RequestHeaderMapPtr&& headers,
                                  AsyncClient::Callbacks& callbacks,
                                  const AsyncClient::RequestOptions& options,
                                  const Upstream::OdCdsContextSharedPtr& discovery_context,
                                  bool end_stream, DiscoveryCallback discovery_callback) {
  ASSERT(discovery_context != nullptr);
  auto request =
      std::make_unique<Request>(*this, cluster, std::move(headers), callbacks, options,
                                discovery_context, end_stream, std::move(discovery_callback));
  auto* result = request.get();
  LinkedList::moveIntoList(std::move(request), requests_);
  result->start();
  return result->finished() ? nullptr : result;
}

} // namespace Http
} // namespace Envoy
