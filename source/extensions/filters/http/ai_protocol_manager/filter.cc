#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include <memory>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

// Matches the media type and ignores parameters, so "application/json;
// charset=utf-8" qualifies but "application/json-seq" does not. Structured
// suffixes ("application/foo+json") are deliberately not matched: widening this
// is a config decision, not a default.
bool isJsonContentType(const Http::RequestHeaderMap& headers) {
  const absl::string_view content_type = headers.getContentTypeValue();
  const absl::string_view json = Http::Headers::get().ContentTypeValues.Json;
  if (!absl::StartsWith(content_type, json)) {
    return false;
  }
  const absl::string_view parameters = content_type.substr(json.size());
  return parameters.empty() || parameters[0] == ';' || parameters[0] == ' ';
}

} // namespace

void AiProtocolManagerFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  // Construct the decode-path manager. Its constructor subscribes to upstream
  // watermarks (via the bridge) so replay can be paced against upstream
  // back-pressure; subscribing may immediately deliver high-watermark callbacks
  // if the upstream is already backed up.
  decode_manager_ = std::make_unique<BufferManager>(
      buffer_factory_, std::make_unique<DecoderFilterChainBridge>(*decoder_callbacks_));
}

void AiProtocolManagerFilter::onDestroy() {
  if (decode_manager_ != nullptr) {
    // Detach the manager (releases the external buffer and unsubscribes from
    // watermarks) but do NOT free it here. onDestroy() can run synchronously while
    // the manager is mid-replay -- a downstream filter answering an injected frame
    // with a local reply reaches destroyFilters() on this very stack -- and freeing
    // the manager then would pull it out from under its own injectData()/read()
    // reentrancy. The manager is owned by unique_ptr and freed when this filter is
    // (deferred-)destroyed, by which point the replay stack has unwound. This honors
    // BufferManager's onDestroy()-before-destruction contract (see buffer_manager.h).
    decode_manager_->onDestroy();
  }
}

Http::FilterHeadersStatus AiProtocolManagerFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                 bool end_stream) {
  // A headers-only request carries no payload to inspect, so there is nothing to
  // hold the chain for: let the headers flow. (Pausing here would also deadlock,
  // since no body would ever arrive to drive the replay that releases them.)
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  route_config_ =
      Http::Utility::resolveMostSpecificPerFilterConfig<RouteConfig>(decoder_callbacks_);
  if (route_config_ != nullptr) {
    ENVOY_LOG(debug, "ai_protocol_manager: route declares schema \"{}\"{}", route_config_->schema(),
              route_config_->normalize() ? ", normalizing" : "");
  }

  // A declared AI endpoint is parsed strictly. Any other route is parsed only if
  // the filter opted into best-effort parsing, and never fails the request.
  const bool parse =
      isJsonContentType(headers) && (route_config_ != nullptr || config_->bestEffortParsing());
  if (parse) {
    reject_on_parse_failure_ = route_config_ != nullptr;
    // Bind to the buffer before any byte arrives: the parser records offsets
    // into it, so it must know which buffer it is describing up front.
    request_json_ = std::make_unique<JsonWithExtBuf>(&decode_manager_->ensureBuffer());
    request_parser_ =
        std::make_unique<JsonWithExtBufParser>(*request_json_, JsonWithExtBufParser::Config{});
  }

  // A body follows: pin the headers at this filter so the subsequent routing and
  // admission filters do not act on them until the payload has been offloaded.
  // decodeData() still fires on this filter while iteration is stopped here, so
  // the BufferManager keeps offloading; the held headers are released when replay
  // injects the first body frame (or, for an empty/trailer-only body, when the
  // BufferManager continues iteration).
  ENVOY_LOG(trace, "ai_protocol_manager: holding headers until payload is offloaded");
  return Http::FilterHeadersStatus::StopIteration;
}

bool AiProtocolManagerFilter::feedParser(const Buffer::Instance& data, bool end_stream) {
  // Feed the frame's slices where they lie: the parser accepts a split at any
  // byte boundary, so there is no need to join them into one block. Copying here
  // would reintroduce the per-stream footprint the external buffer avoids.
  absl::Status status = absl::OkStatus();
  bool fed_this_frame = false;
  const auto slices = data.getRawSlices();
  for (size_t i = 0; i < slices.size() && status.ok(); ++i) {
    // Only the final slice of a terminal frame closes the document.
    const bool last_slice = (i + 1 == slices.size());
    status = request_parser_->feed(
        absl::string_view(static_cast<const char*>(slices[i].mem_), slices[i].len_),
        last_slice && end_stream);
    fed_this_frame = true;
    parser_fed_ = true;
  }
  if (status.ok() && end_stream && !fed_this_frame) {
    // A terminal frame with no bytes (a chunked body ending on its own empty
    // frame) still has to close the document, and no slice above did it.
    status = request_parser_->feed("", /*end_stream=*/true);
  }

  if (!status.ok()) {
    if (reject_on_parse_failure_) {
      rejectInvalidPayload(status);
      return false;
    }
    // Best effort: the payload is forwarded as it stands, just without a
    // document for later filters to work from.
    ENVOY_LOG(debug, "ai_protocol_manager: forwarding unparsed payload: {}", status.message());
    request_parser_.reset();
    request_json_.reset();
  }
  return true;
}

void AiProtocolManagerFilter::rejectInvalidPayload(const absl::Status& status) {
  ENVOY_LOG(debug, "ai_protocol_manager: rejecting request: {}", status.message());
  payload_rejected_ = true;
  request_parser_.reset();
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "invalid JSON payload", nullptr,
                                     std::nullopt, "ai_protocol_manager_invalid_json");
}

Http::FilterDataStatus AiProtocolManagerFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (payload_rejected_) {
    // Already terminated by the local reply; drop whatever is still in flight.
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (request_parser_ != nullptr) {
    if (data.length() == 0 && end_stream && !parser_fed_) {
      // No body at all: pass it through like a headers-only request rather than
      // failing it as an empty document.
      request_parser_.reset();
    } else if (!feedParser(data, end_stream)) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  decode_manager_->onData(data);
  if (end_stream) {
    // The full body has been offloaded. The filter owns replay and end-of-stream:
    // a future change will assemble and inspect the request here (and may replay
    // sub-ranges); for now replay the whole body, then emit the terminal frame.
    decode_manager_->endStream();
    decode_manager_->replay(0, decode_manager_->length(), [this]() {
      // Terminate the stream with an empty end_stream data frame after the replayed
      // body (also releases the held headers when the body was empty).
      Buffer::OwnedImpl end_marker;
      decoder_callbacks_->injectDecodedDataToFilterChain(end_marker, /*end_stream=*/true);
    });
  }
  // Hold the chain here; the BufferManager replays the payload once told to.
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus AiProtocolManagerFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (payload_rejected_) {
    return Http::FilterTrailersStatus::StopIteration;
  }
  // A trailer-only request (no body) has nothing to replay; let the trailers flow.
  if (decode_manager_->empty()) {
    return Http::FilterTrailersStatus::Continue;
  }

  // No data frame carried end_stream, so the document is still open. Closing it
  // here is what catches a truncated payload.
  if (request_parser_ != nullptr) {
    Buffer::OwnedImpl empty;
    if (!feedParser(empty, /*end_stream=*/true)) {
      return Http::FilterTrailersStatus::StopIteration;
    }
  }

  // The body ended without end_stream on a data frame; the trailers carry it.
  decode_manager_->endStream();
  decode_manager_->replay(0, decode_manager_->length(), [this]() {
    // Body fully replayed; release the held trailers (they carry END_STREAM) so
    // they follow the body in order.
    decoder_callbacks_->continueDecoding();
  });
  // Hold the trailers behind the replayed body until the replay-done callback
  // above releases them.
  return Http::FilterTrailersStatus::StopIteration;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
