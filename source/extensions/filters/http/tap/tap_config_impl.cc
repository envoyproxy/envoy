#include "extensions/filters/http/tap/tap_config_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/http.pb.h"

#include "common/common/assert.h"
#include "common/config/version_converter.h"
#include "common/matcher/matcher.h"
#include "common/protobuf/protobuf.h"

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
    new_header.set_value(std::string(header.value().getStringView()));
    return Http::HeaderMap::Iterate::Continue;
  };
}
} // namespace

HttpTapConfigImpl::HttpTapConfigImpl(envoy::config::tap::v3::TapConfig&& proto_config,
                                     Common::Tap::Sink* admin_streamer)
    : TapCommon::TapConfigBaseImpl(std::move(proto_config), admin_streamer) {
  auto* leaf = match_tree_config_.mutable_leaf();
  leaf->mutable_no_match_action()->set_callback("no_match");

  auto* matcher = leaf->add_matchers();
  if (proto_config.has_match()) {
    matcher->mutable_predicate()->MergeFrom(proto_config.match());
  } else {
    envoy::config::common::matcher::v3::MatchPredicate match;
    Config::VersionConverter::upgrade(proto_config.match_config(), match);
    matcher->mutable_predicate()->MergeFrom(match);
  }
  matcher->mutable_action()->set_callback("match");
}

HttpPerRequestTapperPtr HttpTapConfigImpl::createPerRequestTapper(uint64_t stream_id) {
  return std::make_unique<HttpPerRequestTapperImpl>(shared_from_this(), stream_id);
}

void HttpPerRequestTapperImpl::streamRequestHeaders() {
  TapCommon::TraceWrapperPtr trace = makeTraceSegment();
  request_headers_->iterate(fillHeaderList(
      trace->mutable_http_streamed_trace_segment()->mutable_request_headers()->mutable_headers()));
  sink_handle_->submitTrace(std::move(trace));
}

void HttpPerRequestTapperImpl::onRequestHeaders(const Http::RequestHeaderMap& headers) {
  request_headers_ = &headers;
  if (config_->streaming() && tap_match_) {
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
  if (config_->streaming() && tap_match_) {
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
  if (config_->streaming() && tap_match_) {
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
  if (config_->streaming() && tap_match_) {
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
  if (config_->streaming() || !tap_match_) {
    return tap_match_;
  }

  makeBufferedFullTraceIfNeeded();
  auto& http_trace = *buffered_full_trace_->mutable_http_buffered_trace();
  if (request_headers_ != nullptr) {
    request_headers_->iterate(fillHeaderList(http_trace.mutable_request()->mutable_headers()));
  }
  if (request_trailers_ != nullptr) {
    request_trailers_->iterate(fillHeaderList(http_trace.mutable_request()->mutable_trailers()));
  }
  if (response_headers_ != nullptr) {
    response_headers_->iterate(fillHeaderList(http_trace.mutable_response()->mutable_headers()));
  }
  if (response_trailers_ != nullptr) {
    response_trailers_->iterate(fillHeaderList(http_trace.mutable_response()->mutable_trailers()));
  }

  ENVOY_LOG(debug, "submitting buffered trace sink");
  // move is safe as onDestroyLog is the last method called.
  sink_handle_->submitTrace(std::move(buffered_full_trace_));
  return true;
}

void HttpPerRequestTapperImpl::onBody(
    const Buffer::Instance& data, Extensions::Common::Tap::TraceWrapperPtr& buffered_streamed_body,
    uint32_t max_buffered_bytes, MutableBodyChunk mutable_body_chunk,
    MutableMessage mutable_message, bool) {
  // Invoke body matcher.
  if (config_->streaming()) {
    // Without body matching, we must have already started tracing or have not yet matched.
    ASSERT(started_streaming_trace_ || !tap_match_);

    if (started_streaming_trace_) {
      // If we have already started streaming, flush a body segment now.
      TapCommon::TraceWrapperPtr trace = makeTraceSegment();
      TapCommon::Utility::addBufferToProtoBytes(
          *(trace->mutable_http_streamed_trace_segment()->*mutable_body_chunk)(),
          max_buffered_bytes, data, 0, data.length());
      sink_handle_->submitTrace(std::move(trace));
    } else if (!tap_match_failed_) {
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
