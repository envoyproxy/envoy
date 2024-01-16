#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/context.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/router/context.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/server/filter_config.h"
#include "envoy/ssl/connection.h"
#include "envoy/tracing/tracer.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/linked_object.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/null_route_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/router.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/upstream/retry_factory.h"
#include "source/extensions/early_data/default_early_data_policy.h"

namespace Envoy {
namespace Http {
namespace {
// Limit the size of buffer for data used for retries.
// This is currently fixed to 64KB.
constexpr uint64_t kBufferLimitForRetry = 1 << 16;
} // namespace

class AsyncStreamImpl;
class AsyncRequestSharedImpl;

class AsyncClientImpl final : public AsyncClient {
public:
  AsyncClientImpl(Upstream::ClusterInfoConstSharedPtr cluster, Stats::Store& stats_store,
                  Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info,
                  Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                  Random::RandomGenerator& random, Router::ShadowWriterPtr&& shadow_writer,
                  Http::Context& http_context, Router::Context& router_context);
  ~AsyncClientImpl() override;

  // Http::AsyncClient
  Request* send(RequestMessagePtr&& request, Callbacks& callbacks,
                const AsyncClient::RequestOptions& options) override;
  Stream* start(StreamCallbacks& callbacks, const AsyncClient::StreamOptions& options) override;
  OngoingRequest* startRequest(RequestHeaderMapPtr&& request_headers, Callbacks& callbacks,
                               const AsyncClient::RequestOptions& options) override;
  Singleton::Manager& singleton_manager_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }

private:
  template <typename T> T* internalStartRequest(T* async_request);
  Router::FilterConfig config_;
  Event::Dispatcher& dispatcher_;
  std::list<std::unique_ptr<AsyncStreamImpl>> active_streams_;

  friend class AsyncStreamImpl;
  friend class AsyncRequestSharedImpl;
};

/**
 * Implementation of AsyncRequest. This implementation is capable of sending HTTP requests to a
 * ConnectionPool asynchronously.
 */
class AsyncStreamImpl : public virtual AsyncClient::Stream,
                        public StreamDecoderFilterCallbacks,
                        public Event::DeferredDeletable,
                        Logger::Loggable<Logger::Id::http>,
                        public LinkedObject<AsyncStreamImpl>,
                        public ScopeTrackedObject {
public:
  AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                  const AsyncClient::StreamOptions& options);
  ~AsyncStreamImpl() override {
    router_.onDestroy();
    // UpstreamRequest::cleanUp() is guaranteed to reset the high watermark calls.
    ENVOY_BUG(high_watermark_calls_ == 0, "Excess high watermark calls after async stream ended.");
    if (destructor_callback_.has_value()) {
      (*destructor_callback_)();
    }
  }

  void setDestructorCallback(AsyncClient::StreamDestructorCallbacks callback) override {
    ASSERT(!destructor_callback_);
    destructor_callback_.emplace(callback);
  }

  void removeDestructorCallback() override {
    ASSERT(destructor_callback_);
    destructor_callback_.reset();
  }

  void setWatermarkCallbacks(DecoderFilterWatermarkCallbacks& callbacks) override {
    ASSERT(!watermark_callbacks_);
    watermark_callbacks_.emplace(callbacks);
    for (uint32_t i = 0; i < high_watermark_calls_; ++i) {
      watermark_callbacks_->get().onDecoderFilterAboveWriteBufferHighWatermark();
    }
  }

  void removeWatermarkCallbacks() override {
    ASSERT(watermark_callbacks_);
    for (uint32_t i = 0; i < high_watermark_calls_; ++i) {
      watermark_callbacks_->get().onDecoderFilterBelowWriteBufferLowWatermark();
    }
    watermark_callbacks_.reset();
  }

  // Http::AsyncClient::Stream
  void sendHeaders(RequestHeaderMap& headers, bool end_stream) override;
  void sendData(Buffer::Instance& data, bool end_stream) override;
  void sendTrailers(RequestTrailerMap& trailers) override;
  void reset() override;
  bool isAboveWriteBufferHighWatermark() const override { return high_watermark_calls_ > 0; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }

protected:
  bool remoteClosed() { return remote_closed_; }
  void closeLocal(bool end_stream);
  StreamInfo::StreamInfoImpl& streamInfo() override { return stream_info_; }

  AsyncClientImpl& parent_;
  // Callback to listen for stream destruction.
  absl::optional<AsyncClient::StreamDestructorCallbacks> destructor_callback_;
  // Callback to listen for low/high/overflow watermark events.
  absl::optional<std::reference_wrapper<DecoderFilterWatermarkCallbacks>> watermark_callbacks_;

private:
  void cleanup();
  void closeRemote(bool end_stream);
  bool complete() { return local_closed_ && remote_closed_; }

  // Http::StreamDecoderFilterCallbacks
  OptRef<const Network::Connection> connection() override { return {}; }
  Event::Dispatcher& dispatcher() override { return parent_.dispatcher_; }
  void resetStream(Http::StreamResetReason reset_reason = Http::StreamResetReason::LocalReset,
                   absl::string_view transport_failure_reason = "") override;
  Router::RouteConstSharedPtr route() override { return route_; }
  Upstream::ClusterInfoConstSharedPtr clusterInfo() override { return parent_.cluster_; }
  uint64_t streamId() const override { return stream_id_; }
  // TODO(kbaichoo): Plumb account from owning request filter.
  Buffer::BufferMemoryAccountSharedPtr account() const override { return account_; }
  Tracing::Span& activeSpan() override { return active_span_; }
  OptRef<const Tracing::Config> tracingConfig() const override {
    return makeOptRef<const Tracing::Config>(tracing_config_);
  }
  void continueDecoding() override {}
  RequestTrailerMap& addDecodedTrailers() override { PANIC("not implemented"); }
  void addDecodedData(Buffer::Instance&, bool) override {
    // This should only be called if the user has set up buffering. The request is already fully
    // buffered. Note that this is only called via the async client's internal use of the router
    // filter which uses this function for buffering.
    ASSERT(buffered_body_ != nullptr);
  }
  MetadataMapVector& addDecodedMetadata() override { PANIC("not implemented"); }
  void injectDecodedDataToFilterChain(Buffer::Instance&, bool) override {}
  const Buffer::Instance* decodingBuffer() override { return buffered_body_.get(); }
  void modifyDecodingBuffer(std::function<void(Buffer::Instance&)>) override {}
  void sendLocalReply(Code code, absl::string_view body,
                      std::function<void(ResponseHeaderMap& headers)> modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details) override {
    if (encoded_response_headers_) {
      resetStream();
      return;
    }
    Utility::sendLocalReply(
        remote_closed_,
        Utility::EncodeFunctions{nullptr, nullptr,
                                 [this, modify_headers, &details](ResponseHeaderMapPtr&& headers,
                                                                  bool end_stream) -> void {
                                   if (modify_headers != nullptr) {
                                     modify_headers(*headers);
                                   }
                                   encodeHeaders(std::move(headers), end_stream, details);
                                 },
                                 [this](Buffer::Instance& data, bool end_stream) -> void {
                                   encodeData(data, end_stream);
                                 }},
        Utility::LocalReplyData{is_grpc_request_, code, body, grpc_status, is_head_request_});
  }
  // The async client won't pause if sending 1xx headers so simply swallow any.
  void encode1xxHeaders(ResponseHeaderMapPtr&&) override {}
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                     absl::string_view details) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(ResponseTrailerMapPtr&& trailers) override;
  void encodeMetadata(MetadataMapPtr&&) override {}
  void onDecoderFilterAboveWriteBufferHighWatermark() override {
    ++high_watermark_calls_;
    if (watermark_callbacks_.has_value()) {
      watermark_callbacks_->get().onDecoderFilterAboveWriteBufferHighWatermark();
    }
  }
  void onDecoderFilterBelowWriteBufferLowWatermark() override {
    ASSERT(high_watermark_calls_ != 0);
    --high_watermark_calls_;
    if (watermark_callbacks_.has_value()) {
      watermark_callbacks_->get().onDecoderFilterBelowWriteBufferLowWatermark();
    }
  }
  void addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void setDecoderBufferLimit(uint32_t) override {
    IS_ENVOY_BUG("decoder buffer limits should not be overridden on async streams.");
  }
  uint32_t decoderBufferLimit() override { return buffer_limit_.value_or(0); }
  bool recreateStream(const ResponseHeaderMap*) override { return false; }
  const ScopeTrackedObject& scope() override { return *this; }
  void restoreContextOnContinue(ScopeTrackedObjectStack& tracked_object_stack) override {
    tracked_object_stack.add(*this);
  }
  void addUpstreamSocketOptions(const Network::Socket::OptionsSharedPtr&) override {}
  Network::Socket::OptionsSharedPtr getUpstreamSocketOptions() const override { return {}; }
  const Router::RouteSpecificFilterConfig* mostSpecificPerFilterConfig() const override {
    return nullptr;
  }
  void traversePerFilterConfig(
      std::function<void(const Router::RouteSpecificFilterConfig&)>) const override {}
  Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return {}; }
  OptRef<DownstreamStreamFilterCallbacks> downstreamCallbacks() override { return {}; }
  OptRef<UpstreamStreamFilterCallbacks> upstreamCallbacks() override { return {}; }
  void resetIdleTimer() override {}
  void setUpstreamOverrideHost(Upstream::LoadBalancerContext::OverrideHost) override {}
  absl::optional<Upstream::LoadBalancerContext::OverrideHost>
  upstreamOverrideHost() const override {
    return absl::nullopt;
  }
  absl::string_view filterConfigName() const override { return ""; }
  RequestHeaderMapOptRef requestHeaders() override { return makeOptRefFromPtr(request_headers_); }
  RequestTrailerMapOptRef requestTrailers() override {
    return makeOptRefFromPtr(request_trailers_);
  }
  ResponseHeaderMapOptRef informationalHeaders() override { return {}; }
  ResponseHeaderMapOptRef responseHeaders() override { return {}; }
  ResponseTrailerMapOptRef responseTrailers() override { return {}; }

  // ScopeTrackedObject
  void dumpState(std::ostream& os, int indent_level) const override {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "AsyncClient " << this << DUMP_MEMBER(stream_id_) << "\n";
    DUMP_DETAILS(&stream_info_);
  }

  AsyncClient::StreamCallbacks& stream_callbacks_;
  const uint64_t stream_id_;
  Router::ProdFilter router_;
  StreamInfo::StreamInfoImpl stream_info_;
  Tracing::NullSpan active_span_;
  const Tracing::Config& tracing_config_;
  std::shared_ptr<NullRouteImpl> route_;
  uint32_t high_watermark_calls_{};
  bool local_closed_{};
  bool remote_closed_{};
  Buffer::InstancePtr buffered_body_;
  Buffer::BufferMemoryAccountSharedPtr account_{nullptr};
  absl::optional<uint32_t> buffer_limit_{absl::nullopt};
  RequestHeaderMap* request_headers_{};
  RequestTrailerMap* request_trailers_{};
  bool encoded_response_headers_{};
  bool is_grpc_request_{};
  bool is_head_request_{false};
  bool send_xff_{true};

  friend class AsyncClientImpl;
  friend class AsyncClientImplUnitTest;
};

class AsyncRequestSharedImpl : public virtual AsyncClient::Request,
                               protected AsyncStreamImpl,
                               protected AsyncClient::StreamCallbacks {
public:
  void cancel() final;

protected:
  AsyncRequestSharedImpl(AsyncClientImpl& parent, AsyncClient::Callbacks& callbacks,
                         const AsyncClient::RequestOptions& options);
  void onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) final;
  void onData(Buffer::Instance& data, bool end_stream) final;
  void onTrailers(ResponseTrailerMapPtr&& trailers) final;
  void onComplete() final;
  void onReset() final;

  AsyncClient::Callbacks& callbacks_;
  Tracing::SpanPtr child_span_;
  std::unique_ptr<ResponseMessageImpl> response_;
  bool cancelled_{};
};

class AsyncOngoingRequestImpl final : public AsyncClient::OngoingRequest,
                                      public AsyncRequestSharedImpl {
public:
  AsyncOngoingRequestImpl(RequestHeaderMapPtr&& request_headers, AsyncClientImpl& parent,
                          AsyncClient::Callbacks& callbacks,
                          const AsyncClient::RequestOptions& options)
      : AsyncRequestSharedImpl(parent, callbacks, options),
        request_headers_(std::move(request_headers)) {
    ASSERT(request_headers_);
  }
  void captureAndSendTrailers(RequestTrailerMapPtr&& trailers) override {
    request_trailers_ = std::move(trailers);
    sendTrailers(*request_trailers_);
  }

private:
  void initialize();

  RequestHeaderMapPtr request_headers_;
  RequestTrailerMapPtr request_trailers_;

  friend class AsyncClientImpl;
};

class AsyncRequestImpl final : public AsyncRequestSharedImpl {
public:
  AsyncRequestImpl(RequestMessagePtr&& request, AsyncClientImpl& parent,
                   AsyncClient::Callbacks& callbacks, const AsyncClient::RequestOptions& options)
      : AsyncRequestSharedImpl(parent, callbacks, options), request_(std::move(request)) {}

private:
  void initialize();

  // Http::StreamDecoderFilterCallbacks
  void addDecodedData(Buffer::Instance&, bool) override {
    // The request is already fully buffered. Note that this is only called via the async client's
    // internal use of the router filter which uses this function for buffering.
  }
  const Buffer::Instance* decodingBuffer() override { return &request_->body(); }
  void modifyDecodingBuffer(std::function<void(Buffer::Instance&)>) override {}

  RequestMessagePtr request_;

  friend class AsyncClientImpl;
};

} // namespace Http
} // namespace Envoy
