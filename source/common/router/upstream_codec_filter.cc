#include "source/common/router/upstream_codec_filter.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/status.h"
#include "envoy/http/header_map.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Router {

void UpstreamCodecFilter::onBelowWriteBufferLowWatermark() {
  callbacks_->clusterInfo()->trafficStats().upstream_flow_control_resumed_reading_total_.inc();
  callbacks_->upstreamCallbacks()->upstream()->readDisable(false);
}

void UpstreamCodecFilter::onAboveWriteBufferHighWatermark() {
  callbacks_->clusterInfo()->trafficStats().upstream_flow_control_paused_reading_total_.inc();
  callbacks_->upstreamCallbacks()->upstream()->readDisable(true);
}

void UpstreamCodecFilter::onUpstreamConnectionEstablished() {
  if (latched_end_stream_.has_value()) {
    const bool end_stream = *latched_end_stream_;
    latched_end_stream_.reset();
    Http::FilterHeadersStatus status = decodeHeaders(*latched_headers_, end_stream);
    if (status == Http::FilterHeadersStatus::Continue) {
      callbacks_->continueDecoding();
    }
  }
}

// This is the last stop in the filter chain: take the headers and ship them to the codec.
Http::FilterHeadersStatus UpstreamCodecFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                             bool end_stream) {
  ASSERT(callbacks_->upstreamCallbacks());
  if (!callbacks_->upstreamCallbacks()->upstream()) {
    latched_headers_ = headers;
    latched_end_stream_ = end_stream;
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }

  ENVOY_STREAM_LOG(trace, "proxying headers", *callbacks_);
  calling_encode_headers_ = true;
  const Http::Status status =
      callbacks_->upstreamCallbacks()->upstream()->encodeHeaders(headers, end_stream);

  calling_encode_headers_ = false;
  if (!status.ok() || deferred_reset_) {
    deferred_reset_ = false;
    // It is possible that encodeHeaders() fails. This can happen if filters or other extensions
    // erroneously remove required headers.
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError);
    const std::string details =
        absl::StrCat(StreamInfo::ResponseCodeDetails::get().FilterRemovedRequiredRequestHeaders,
                     "{", StringUtil::replaceAllEmptySpace(status.message()), "}");
    callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, status.message(), nullptr,
                               absl::nullopt, details);
    return Http::FilterHeadersStatus::StopIteration;
  }
  upstreamTiming().onFirstUpstreamTxByteSent(callbacks_->dispatcher().timeSource());

  if (end_stream) {
    upstreamTiming().onLastUpstreamTxByteSent(callbacks_->dispatcher().timeSource());
  }
  if (callbacks_->upstreamCallbacks()->pausedForConnect()) {
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }
  return Http::FilterHeadersStatus::Continue;
}

// This is the last stop in the filter chain: take the data and ship it to the codec.
Http::FilterDataStatus UpstreamCodecFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!callbacks_->upstreamCallbacks()->pausedForConnect());
  ENVOY_STREAM_LOG(trace, "proxying {} bytes", *callbacks_, data.length());
  callbacks_->upstreamCallbacks()->upstreamStreamInfo().addBytesSent(data.length());
  // TODO(alyssawilk) test intermediate filters calling continue.
  callbacks_->upstreamCallbacks()->upstream()->encodeData(data, end_stream);
  if (end_stream) {
    upstreamTiming().onLastUpstreamTxByteSent(callbacks_->dispatcher().timeSource());
  }
  return Http::FilterDataStatus::Continue;
}

// This is the last stop in the filter chain: take the trailers and ship them to the codec.
Http::FilterTrailersStatus UpstreamCodecFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  ASSERT(!callbacks_->upstreamCallbacks()->pausedForConnect());
  ENVOY_STREAM_LOG(trace, "proxying trailers", *callbacks_);
  callbacks_->upstreamCallbacks()->upstream()->encodeTrailers(trailers);
  upstreamTiming().onLastUpstreamTxByteSent(callbacks_->dispatcher().timeSource());
  return Http::FilterTrailersStatus::Continue;
}

// This is the last stop in the filter chain: take the metadata and ship them to the codec.
Http::FilterMetadataStatus UpstreamCodecFilter::decodeMetadata(Http::MetadataMap& metadata_map) {
  ASSERT(!callbacks_->upstreamCallbacks()->pausedForConnect());
  ENVOY_STREAM_LOG(trace, "proxying metadata", *callbacks_);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.emplace_back(std::make_unique<Http::MetadataMap>(metadata_map));
  callbacks_->upstreamCallbacks()->upstream()->encodeMetadata(metadata_map_vector);
  return Http::FilterMetadataStatus::Continue;
}

// Store the callbacks from the UpstreamFilterManager, for sending the response to.
void UpstreamCodecFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  callbacks_->addDownstreamWatermarkCallbacks(*this);
  callbacks_->upstreamCallbacks()->addUpstreamCallbacks(*this);
  callbacks_->upstreamCallbacks()->setUpstreamToDownstream(bridge_);
}

// This is the response 1xx headers arriving from the codec. Send them through the filter manager.
void UpstreamCodecFilter::CodecBridge::decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) {
  // The filter manager can not handle more than 1 1xx header, so only forward
  // the first one.
  if (!seen_1xx_headers_) {
    seen_1xx_headers_ = true;
    filter_.callbacks_->encode1xxHeaders(std::move(headers));
  }
}

// This is the response headers arriving from the codec. Send them through the filter manager.
void UpstreamCodecFilter::CodecBridge::decodeHeaders(Http::ResponseHeaderMapPtr&& headers,
                                                     bool end_stream) {
  // TODO(rodaine): This is actually measuring after the headers are parsed and not the first
  // byte.
  filter_.upstreamTiming().onFirstUpstreamRxByteReceived(
      filter_.callbacks_->dispatcher().timeSource());

  if (filter_.callbacks_->upstreamCallbacks()->pausedForConnect() &&
      Http::Utility::getResponseStatus(*headers) == 200) {
    filter_.callbacks_->upstreamCallbacks()->setPausedForConnect(false);
    filter_.callbacks_->continueDecoding();
  }

  maybeEndDecode(end_stream);
  filter_.callbacks_->encodeHeaders(std::move(headers), end_stream,
                                    StreamInfo::ResponseCodeDetails::get().ViaUpstream);
}

// This is response data arriving from the codec. Send it through the filter manager.
void UpstreamCodecFilter::CodecBridge::decodeData(Buffer::Instance& data, bool end_stream) {
  maybeEndDecode(end_stream);
  filter_.callbacks_->encodeData(data, end_stream);
}

// This is response trailers arriving from the codec. Send them through the filter manager.
void UpstreamCodecFilter::CodecBridge::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  maybeEndDecode(true);
  filter_.callbacks_->encodeTrailers(std::move(trailers));
}

// This is response metadata arriving from the codec. Send it through the filter manager.
void UpstreamCodecFilter::CodecBridge::decodeMetadata(Http::MetadataMapPtr&& metadata_map) {
  filter_.callbacks_->encodeMetadata(std::move(metadata_map));
}

void UpstreamCodecFilter::CodecBridge::dumpState(std::ostream& os, int indent_level) const {
  filter_.callbacks_->upstreamCallbacks()->dumpState(os, indent_level);
}

void UpstreamCodecFilter::CodecBridge::maybeEndDecode(bool end_stream) {
  if (end_stream) {
    filter_.upstreamTiming().onLastUpstreamRxByteReceived(
        filter_.callbacks_->dispatcher().timeSource());
  }
}

REGISTER_FACTORY(UpstreamCodecFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace Router
} // namespace Envoy
