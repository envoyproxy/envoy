#pragma once

#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/http.pb.h"
#include "envoy/extensions/filters/http/tap/v3/tap.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/tap/tap_config_base.h"
#include "source/extensions/filters/http/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

class HttpTapConfigImpl : public Extensions::Common::Tap::TapConfigBaseImpl,
                          public HttpTapConfig,
                          public std::enable_shared_from_this<HttpTapConfigImpl> {
public:
  HttpTapConfigImpl(const envoy::config::tap::v3::TapConfig& proto_config,
                    Extensions::Common::Tap::Sink* admin_streamer,
                    Server::Configuration::FactoryContext& context);

  // TapFilter::HttpTapConfig
  HttpPerRequestTapperPtr
  createPerRequestTapper(const envoy::extensions::filters::http::tap::v3::Tap& tap_config,
                         uint64_t stream_id, OptRef<const Network::Connection> connection) override;

  TimeSource& timeSource() const override { return time_source_; }

private:
  TimeSource& time_source_;
};

class HttpPerRequestTapperImpl : public HttpPerRequestTapper, Logger::Loggable<Logger::Id::tap> {
public:
  HttpPerRequestTapperImpl(HttpTapConfigSharedPtr config,
                           const envoy::extensions::filters::http::tap::v3::Tap& tap_config,
                           uint64_t stream_id, OptRef<const Network::Connection> connection)
      : config_(std::move(config)),
        should_record_headers_received_time_(tap_config.record_headers_received_time()),
        should_record_downstream_connection_(tap_config.record_downstream_connection()),
        stream_id_(stream_id), sink_handle_(config_->createPerTapSinkHandleManager(stream_id)),
        statuses_(config_->createMatchStatusVector()), connection_(connection) {
    config_->rootMatcher().onNewStream(statuses_);
  }

  // TapFilter::HttpPerRequestTapper
  void onRequestHeaders(const Http::RequestHeaderMap& headers) override;
  void onRequestBody(const Buffer::Instance& data) override;
  void onRequestTrailers(const Http::RequestTrailerMap& headers) override;
  void onResponseHeaders(const Http::ResponseHeaderMap& headers) override;
  void onResponseBody(const Buffer::Instance& data) override;
  void onResponseTrailers(const Http::ResponseTrailerMap& headers) override;
  bool onDestroyLog() override;

private:
  using HttpStreamedTraceSegment = envoy::data::tap::v3::HttpStreamedTraceSegment;
  using MutableBodyChunk = envoy::data::tap::v3::Body* (HttpStreamedTraceSegment::*)();
  using HttpBufferedTrace = envoy::data::tap::v3::HttpBufferedTrace;
  using MutableMessage = envoy::data::tap::v3::HttpBufferedTrace::Message* (HttpBufferedTrace::*)();

  void onBody(const Buffer::Instance& data,
              Extensions::Common::Tap::TraceWrapperPtr& buffered_streamed_body,
              uint32_t max_buffered_bytes, MutableBodyChunk mutable_body_chunk,
              MutableMessage mutable_message, bool request);

  void makeBufferedFullTraceIfNeeded() {
    if (buffered_full_trace_ == nullptr) {
      buffered_full_trace_ = Extensions::Common::Tap::makeTraceWrapper();
    }
  }

  Extensions::Common::Tap::TraceWrapperPtr makeTraceSegment() {
    Extensions::Common::Tap::TraceWrapperPtr segment = Extensions::Common::Tap::makeTraceWrapper();
    segment->mutable_http_streamed_trace_segment()->set_trace_id(stream_id_);
    return segment;
  }

  void streamRequestHeaders();
  void streamBufferedRequestBody();
  void streamRequestTrailers();
  void streamResponseHeaders();
  void streamBufferedResponseBody();

  // Functions for request/response caught time stamp
  void setTimeStamp(long& timestamp) {
    timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    config_->timeSource().systemTime().time_since_epoch())
                    .count();
  }

  HttpTapConfigSharedPtr config_;
  const bool should_record_headers_received_time_;
  const bool should_record_downstream_connection_;
  const uint64_t stream_id_;
  Extensions::Common::Tap::PerTapSinkHandleManagerPtr sink_handle_;
  Extensions::Common::Tap::Matcher::MatchStatusVector statuses_;
  OptRef<const Network::Connection> connection_;
  bool started_streaming_trace_{};
  long request_headers_received_time_;
  long response_headers_received_time_;
  const Http::RequestHeaderMap* request_headers_{};
  const Http::HeaderMap* request_trailers_{};
  const Http::ResponseHeaderMap* response_headers_{};
  const Http::ResponseTrailerMap* response_trailers_{};
  // Must be a shared_ptr because of submitTrace().
  Extensions::Common::Tap::TraceWrapperPtr buffered_streamed_request_body_;
  Extensions::Common::Tap::TraceWrapperPtr buffered_streamed_response_body_;
  Extensions::Common::Tap::TraceWrapperPtr buffered_full_trace_;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
