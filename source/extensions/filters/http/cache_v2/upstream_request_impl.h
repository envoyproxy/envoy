#pragma once

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cancel_wrapper.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/cache_v2/http_source.h"
#include "source/extensions/filters/http/cache_v2/range_utils.h"
#include "source/extensions/filters/http/cache_v2/stats.h"
#include "source/extensions/filters/http/cache_v2/upstream_request.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class UpstreamRequestImpl : public Logger::Loggable<Logger::Id::cache_filter>,
                            public UpstreamRequest,
                            public Http::AsyncClient::StreamCallbacks {
public:
  // Called from the factory.
  void sendHeaders(Http::RequestHeaderMapPtr request_headers) override;
  // HttpSource.
  void getHeaders(GetHeadersCallback&& cb) override;
  // Though range is an argument here, only the length is used by UpstreamRequest
  // - the pieces requested should always be in order so we can just consume the
  // stream as it comes.
  void getBody(AdjustedByteRange range, GetBodyCallback&& cb) override;
  void getTrailers(GetTrailersCallback&& cb) override;

  // StreamCallbacks
  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void onComplete() override;
  void onReset() override;

  // Called by WatermarkBuffer
  void onAboveHighWatermark();
  void onBelowLowWatermark();

  ~UpstreamRequestImpl() override;

private:
  friend class UpstreamRequestImplFactory;
  UpstreamRequestImpl(Event::Dispatcher& dispatcher, Http::AsyncClient& async_client,
                      const Http::AsyncClient::StreamOptions& options,
                      const std::shared_ptr<const CacheFilterStatsProvider> stats_provider);
  // If the headers and callback are both present, call the callback.
  void maybeDeliverHeaders();

  // If the required body chunk and callback are both present, call the callback.
  void maybeDeliverBody();

  // If the trailers and callback are both present, call the callback.
  void maybeDeliverTrailers();

  using CallbackTypes =
      absl::variant<absl::monostate, GetHeadersCallback, GetBodyCallback, GetTrailersCallback>;

  // Returns the current callback and clears the member variable so it's safe to
  // assert that it's empty.
  CallbackTypes consumeCallback() { return std::exchange(callback_, absl::monostate{}); }

  CacheFilterStats& stats() const { return stats_provider_->stats(); }

  Event::Dispatcher& dispatcher_;
  Http::AsyncClient::Stream* stream_;
  Http::RequestHeaderMapPtr request_headers_;
  Http::ResponseHeaderMapPtr headers_;
  CallbackTypes callback_;
  bool end_stream_after_headers_{false};
  Buffer::WatermarkBuffer body_buffer_;
  AdjustedByteRange requested_body_range_{0, 1};
  uint64_t stream_pos_ = 0;
  bool end_stream_after_body_{false};
  Http::ResponseTrailerMapPtr trailers_;
  CancelWrapper::CancelFunction cancel_ = []() {};
  const std::shared_ptr<const CacheFilterStatsProvider> stats_provider_;
};

class UpstreamRequestImplFactory : public UpstreamRequestFactory {
public:
  UpstreamRequestImplFactory(Event::Dispatcher& dispatcher, Http::AsyncClient& async_client,
                             Http::AsyncClient::StreamOptions stream_options)
      : dispatcher_(dispatcher), async_client_(async_client),
        stream_options_(std::move(stream_options)) {}

  UpstreamRequestPtr
  create(const std::shared_ptr<const CacheFilterStatsProvider> stats_provider) override;

private:
  Event::Dispatcher& dispatcher_;
  Http::AsyncClient& async_client_;
  Http::AsyncClient::StreamOptions stream_options_;
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
