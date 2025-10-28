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
  callbacks_->clusterInfo()->trafficStats()->upstream_flow_control_resumed_reading_total_.inc();
  callbacks_->upstreamCallbacks()->upstream()->readDisable(false);
}

void UpstreamCodecFilter::onAboveWriteBufferHighWatermark() {
  callbacks_->clusterInfo()->trafficStats()->upstream_flow_control_paused_reading_total_.inc();
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
  if (!status.ok() || !deferred_reset_status_.ok()) {
    // It is possible that encodeHeaders() fails. This can happen if filters or other extensions
    // erroneously remove required headers.
    callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::DownstreamProtocolError);
    const std::string details =
        deferred_reset_status_.ok()
            ? absl::StrCat(
                  StreamInfo::ResponseCodeDetails::get().FilterRemovedRequiredRequestHeaders, "{",
                  StringUtil::replaceAllEmptySpace(status.message()), "}")
            : absl::StrCat(StreamInfo::ResponseCodeDetails::get().EarlyUpstreamReset, "{",
                           StringUtil::replaceAllEmptySpace(deferred_reset_status_.message()), "}");
    deferred_reset_status_ = absl::OkStatus();
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
  } else if (callbacks_->upstreamCallbacks()->pausedForWebsocketUpgrade()) {
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
      ((Http::CodeUtility::is2xx(Http::Utility::getResponseStatus(*headers))))) {
    filter_.callbacks_->upstreamCallbacks()->setPausedForConnect(false);
    filter_.callbacks_->continueDecoding();
  }

  if (filter_.callbacks_->upstreamCallbacks()->pausedForWebsocketUpgrade()) {
    const uint64_t status = Http::Utility::getResponseStatus(*headers);
    const auto protocol = filter_.callbacks_->upstreamCallbacks()->upstreamStreamInfo().protocol();
    if (status == static_cast<uint64_t>(Http::Code::SwitchingProtocols) ||
        (protocol.has_value() && protocol.value() != Envoy::Http::Protocol::Http11)) {
      // handshake is finished and continue the data processing.
      filter_.callbacks_->upstreamCallbacks()->setPausedForWebsocketUpgrade(false);
      // Disable the route timeout since the websocket upgrade completed successfully
      filter_.callbacks_->upstreamCallbacks()->disableRouteTimeoutForWebsocketUpgrade();
      // Disable per-try timeouts since the websocket upgrade completed successfully
      filter_.callbacks_->upstreamCallbacks()->disablePerTryTimeoutForWebsocketUpgrade();
      filter_.callbacks_->continueDecoding();
    } else if (Runtime::runtimeFeatureEnabled(
                   "envoy.reloadable_features.websocket_allow_4xx_5xx_through_filter_chain") &&
               status >= 400) {
      maybeEndDecode(end_stream);
      filter_.callbacks_->encodeHeaders(std::move(headers), end_stream,
                                        StreamInfo::ResponseCodeDetails::get().ViaUpstream);
      return;
    } else {
      // Other status, e.g., 200, indicate a failed handshake, Envoy as a proxy will proxy
      // back the response header to downstream and then close the request, since WebSocket
      // just needs headers for handshake per RFC-6455. Note: HTTP/2 200 will be normalized to
      // 101 before this point in codec and this patch will skip this scenario from the above
      // proto check.
      filter_.callbacks_->sendLocalReply(
          static_cast<Envoy::Http::Code>(status), "",
          [&headers](Http::ResponseHeaderMap& local_headers) {
            headers->iterate([&local_headers](const Envoy::Http::HeaderEntry& header) {
              local_headers.addCopy(Http::LowerCaseString(header.key().getStringView()),
                                    header.value().getStringView());
              return Envoy::Http::HeaderMap::Iterate::Continue;
            });
          },
          std::nullopt, StreamInfo::ResponseCodeDetails::get().WebsocketHandshakeUnsuccessful);
      return;
    }
  }

  maybeEndDecode(end_stream);
  filter_.callbacks_->encodeHeaders(std::move(headers), end_stream,
                                    StreamInfo::ResponseCodeDetails::get().ViaUpstream);
}

// This is response data arriving from the codec. Send it through the filter manager.
void UpstreamCodecFilter::CodecBridge::decodeData(Buffer::Instance& data, bool end_stream) {
  // Record the time when the first byte of response body is received.
  if (!first_body_rx_recorded_) {
    first_body_rx_recorded_ = true;
    filter_.upstreamTiming().onFirstUpstreamRxBodyByteReceived(
        filter_.callbacks_->dispatcher().timeSource());
  }

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

void UpstreamCodecFilter::CodecBridge::onResetStream(Http::StreamResetReason reason,
                                                     absl::string_view transport_failure_reason) {
  if (filter_.calling_encode_headers_) {
    // If called while still encoding errors, the reset reason won't be appended to the details
    // string through the reset stream call, so append it here.
    std::string failure_reason(Http::Utility::resetReasonToString(reason));
    if (!transport_failure_reason.empty()) {
      absl::StrAppend(&failure_reason, "|", transport_failure_reason);
    }
    filter_.deferred_reset_status_ = absl::InternalError(failure_reason);
    return;
  }

  std::string failure_reason(transport_failure_reason);
  if (reason == Http::StreamResetReason::LocalReset) {
    failure_reason = absl::StrCat(transport_failure_reason, "|codec_error");
  }
  filter_.callbacks_->resetStream(reason, failure_reason);
}

REGISTER_FACTORY(UpstreamCodecFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

class DefaultUpstreamHttpFilterChainFactory : public Http::FilterChainFactory {
public:
  DefaultUpstreamHttpFilterChainFactory()
      : factory_([](Http::FilterChainFactoryCallbacks& callbacks) -> void {
          callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
        }) {}

  bool createFilterChain(
      Http::FilterChainManager& manager,
      const Http::FilterChainOptions& = Http::EmptyFilterChainOptions{}) const override {
    manager.applyFilterFactoryCb({"envoy.filters.http.upstream_codec"}, factory_);
    return true;
  }
  bool createUpgradeFilterChain(absl::string_view, const UpgradeMap*, Http::FilterChainManager&,
                                const Http::FilterChainOptions&) const override {
    // Upgrade filter chains not yet supported for upstream HTTP filters.
    return false;
  }

private:
  mutable Http::FilterFactoryCb factory_;
};

const Http::FilterChainFactory& defaultUpstreamHttpFilterChainFactory() {
  CONSTRUCT_ON_FIRST_USE(DefaultUpstreamHttpFilterChainFactory);
}

} // namespace Router
} // namespace Envoy
