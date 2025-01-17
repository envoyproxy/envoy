#include "source/extensions/filters/http/tap/tap_config_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/http.pb.h"

#include "source/common/common/assert.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

namespace TapCommon = Extensions::Common::Tap;

namespace {
Http::HeaderMap::ConstIterateCb
fillHeaderList(Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>* output) {
  return [output](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto& new_header = *output->Add();
    new_header.set_key(std::string(header.key().getStringView()));
    new_header.set_value(MessageUtil::sanitizeUtf8String(header.value().getStringView()));

    return Http::HeaderMap::Iterate::Continue;
  };
}
} // namespace

HttpTapConfigImpl::HttpTapConfigImpl(const envoy::config::tap::v3::TapConfig& proto_config,
                                     Common::Tap::Sink* admin_streamer,
                                     Server::Configuration::FactoryContext& context)
    : TapCommon::TapConfigBaseImpl(std::move(proto_config), admin_streamer, context),
      time_source_(context.serverFactoryContext().mainThreadDispatcher().timeSource()) {}

HttpPerRequestTapperPtr HttpTapConfigImpl::createPerRequestTapper(
    const envoy::extensions::filters::http::tap::v3::Tap& tap_config, uint64_t stream_id,
    OptRef<const Network::Connection> connection) {
  return std::make_unique<HttpPerRequestTapperImpl>(shared_from_this(), tap_config, stream_id,
                                                    connection);
}

void HttpPerRequestTapperImpl::streamRequestHeaders() {
  TapCommon::TraceWrapperPtr trace = makeTraceSegment();
  request_headers_->iterate(fillHeaderList(
      trace->mutable_http_streamed_trace_segment()->mutable_request_headers()->mutable_headers()));
  sink_handle_->submitTrace(std::move(trace));
}

void HttpPerRequestTapperImpl::onRequestHeaders(const Http::RequestHeaderMap& headers) {
  request_headers_ = &headers;
  if (should_record_headers_received_time_) {
    setTimeStamp(request_headers_received_time_);
  }

  config_->rootMatcher().onHttpRequestHeaders(headers, statuses_);
  if (config_->streaming() && config_->rootMatcher().matchStatus(statuses_).matches_) {
    ASSERT(!started_streaming_trace_);
    started_streaming_trace_ = true;
    streamRequestHeaders();
  }
}

void HttpPerRequestTapperImpl::streamBufferedRequestBody() {
  if (buffered_streamed_request_body_ != nullptr) {
    sink_handle_->submitTrace(std::move(buffered_streamed_request_body_));
    buffered_streamed_request_body_.reset();
  }
}

void HttpPerRequestTapperImpl::onRequestBody(const Buffer::Instance& data) {
  onBody(data, buffered_streamed_request_body_, config_->maxBufferedRxBytes(),
         &envoy::data::tap::v3::HttpStreamedTraceSegment::mutable_request_body_chunk,
         &envoy::data::tap::v3::HttpBufferedTrace::mutable_request, true);
}

void HttpPerRequestTapperImpl::streamRequestTrailers() {
  if (request_trailers_ != nullptr) {
    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    request_trailers_->iterate(fillHeaderList(trace->mutable_http_streamed_trace_segment()
                                                  ->mutable_request_trailers()
                                                  ->mutable_headers()));
    sink_handle_->submitTrace(std::move(trace));
  }
}

void HttpPerRequestTapperImpl::onRequestTrailers(const Http::RequestTrailerMap& trailers) {
  request_trailers_ = &trailers;
  config_->rootMatcher().onHttpRequestTrailers(trailers, statuses_);
  if (config_->streaming() && config_->rootMatcher().matchStatus(statuses_).matches_) {
    if (!started_streaming_trace_) {
      started_streaming_trace_ = true;
      // Flush anything that we already buffered.
      streamRequestHeaders();
      streamBufferedRequestBody();
    }

    streamRequestTrailers();
  }
}

void HttpPerRequestTapperImpl::streamResponseHeaders() {
  TapCommon::TraceWrapperPtr trace = makeTraceSegment();
  response_headers_->iterate(fillHeaderList(
      trace->mutable_http_streamed_trace_segment()->mutable_response_headers()->mutable_headers()));
  sink_handle_->submitTrace(std::move(trace));
}

void HttpPerRequestTapperImpl::onResponseHeaders(const Http::ResponseHeaderMap& headers) {
  response_headers_ = &headers;
  if (should_record_headers_received_time_) {
    setTimeStamp(response_headers_received_time_);
  }

  config_->rootMatcher().onHttpResponseHeaders(headers, statuses_);
  if (config_->streaming() && config_->rootMatcher().matchStatus(statuses_).matches_) {
    if (!started_streaming_trace_) {
      started_streaming_trace_ = true;
      // Flush anything that we already buffered.
      streamRequestHeaders();
      streamBufferedRequestBody();
      streamRequestTrailers();
    }

    streamResponseHeaders();
  }
}

void HttpPerRequestTapperImpl::streamBufferedResponseBody() {
  if (buffered_streamed_response_body_ != nullptr) {
    sink_handle_->submitTrace(std::move(buffered_streamed_response_body_));
    buffered_streamed_response_body_.reset();
  }
}

void HttpPerRequestTapperImpl::onResponseBody(const Buffer::Instance& data) {
  onBody(data, buffered_streamed_response_body_, config_->maxBufferedTxBytes(),
         &envoy::data::tap::v3::HttpStreamedTraceSegment::mutable_response_body_chunk,
         &envoy::data::tap::v3::HttpBufferedTrace::mutable_response, false);
}

void HttpPerRequestTapperImpl::onResponseTrailers(const Http::ResponseTrailerMap& trailers) {
  response_trailers_ = &trailers;
  config_->rootMatcher().onHttpResponseTrailers(trailers, statuses_);
  if (config_->streaming() && config_->rootMatcher().matchStatus(statuses_).matches_) {
    if (!started_streaming_trace_) {
      started_streaming_trace_ = true;
      // Flush anything that we already buffered.
      streamRequestHeaders();
      streamBufferedRequestBody();
      streamRequestTrailers();
      streamResponseHeaders();
      streamBufferedResponseBody();
    }

    TapCommon::TraceWrapperPtr trace = makeTraceSegment();
    trailers.iterate(fillHeaderList(trace->mutable_http_streamed_trace_segment()
                                        ->mutable_response_trailers()
                                        ->mutable_headers()));
    sink_handle_->submitTrace(std::move(trace));
  }
}

bool HttpPerRequestTapperImpl::onDestroyLog() {
  if (config_->streaming() || !config_->rootMatcher().matchStatus(statuses_).matches_) {
    return config_->rootMatcher().matchStatus(statuses_).matches_;
  }

  makeBufferedFullTraceIfNeeded();
  auto& http_trace = *buffered_full_trace_->mutable_http_buffered_trace();
  if (request_headers_ != nullptr) {
    request_headers_->iterate(fillHeaderList(http_trace.mutable_request()->mutable_headers()));
    if (should_record_headers_received_time_) {
      http_trace.mutable_request()->mutable_headers_received_time()->MergeFrom(
          Protobuf::util::TimeUtil::NanosecondsToTimestamp(request_headers_received_time_));
    }
  }
  if (request_trailers_ != nullptr) {
    request_trailers_->iterate(fillHeaderList(http_trace.mutable_request()->mutable_trailers()));
  }
  if (response_headers_ != nullptr) {
    response_headers_->iterate(fillHeaderList(http_trace.mutable_response()->mutable_headers()));
    if (should_record_headers_received_time_) {
      http_trace.mutable_response()->mutable_headers_received_time()->MergeFrom(
          Protobuf::util::TimeUtil::NanosecondsToTimestamp(response_headers_received_time_));
    }
  }
  if (response_trailers_ != nullptr) {
    response_trailers_->iterate(fillHeaderList(http_trace.mutable_response()->mutable_trailers()));
  }

  if (should_record_downstream_connection_ && connection_.has_value()) {

    envoy::config::core::v3::Address downstream_local_address;
    envoy::config::core::v3::Address downstream_remote_address;

    Envoy::Network::Utility::addressToProtobufAddress(
        *connection_->connectionInfoProvider().localAddress(), downstream_local_address);
    Envoy::Network::Utility::addressToProtobufAddress(
        *connection_->connectionInfoProvider().remoteAddress(), downstream_remote_address);

    http_trace.mutable_downstream_connection()->mutable_local_address()->MergeFrom(
        downstream_local_address);
    http_trace.mutable_downstream_connection()->mutable_remote_address()->MergeFrom(
        downstream_remote_address);
  }

  ENVOY_LOG(debug, "submitting buffered trace sink");
  // move is safe as onDestroyLog is the last method called.
  sink_handle_->submitTrace(std::move(buffered_full_trace_));
  return true;
}

void HttpPerRequestTapperImpl::onBody(
    const Buffer::Instance& data, Extensions::Common::Tap::TraceWrapperPtr& buffered_streamed_body,
    uint32_t max_buffered_bytes, MutableBodyChunk mutable_body_chunk,
    MutableMessage mutable_message, bool request) {
  // Invoke body matcher.
  request ? config_->rootMatcher().onRequestBody(data, statuses_)
          : config_->rootMatcher().onResponseBody(data, statuses_);
  if (config_->streaming()) {
    const auto& match_status = config_->rootMatcher().matchStatus(statuses_);
    // Without body matching, we must have already started tracing or have not yet matched.
    ASSERT(started_streaming_trace_ || !match_status.matches_);

    if (started_streaming_trace_) {
      // If we have already started streaming, flush a body segment now.
      TapCommon::TraceWrapperPtr trace = makeTraceSegment();
      TapCommon::Utility::addBufferToProtoBytes(
          *(trace->mutable_http_streamed_trace_segment()->*mutable_body_chunk)(),
          max_buffered_bytes, data, 0, data.length());
      sink_handle_->submitTrace(std::move(trace));
    } else if (match_status.might_change_status_) {
      // If we might still match, start buffering the body up to our limit.
      if (buffered_streamed_body == nullptr) {
        buffered_streamed_body = makeTraceSegment();
      }
      auto& body =
          *(buffered_streamed_body->mutable_http_streamed_trace_segment()->*mutable_body_chunk)();
      ASSERT(body.as_bytes().size() <= max_buffered_bytes);
      TapCommon::Utility::addBufferToProtoBytes(body, max_buffered_bytes - body.as_bytes().size(),
                                                data, 0, data.length());
    }
  } else {
    // If we are not streaming, buffer the body up to our limit.
    makeBufferedFullTraceIfNeeded();
    auto& body =
        *(buffered_full_trace_->mutable_http_buffered_trace()->*mutable_message)()->mutable_body();
    ASSERT(body.as_bytes().size() <= max_buffered_bytes);
    TapCommon::Utility::addBufferToProtoBytes(body, max_buffered_bytes - body.as_bytes().size(),
                                              data, 0, data.length());
  }
}
} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
