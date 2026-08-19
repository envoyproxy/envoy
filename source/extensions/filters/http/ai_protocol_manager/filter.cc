#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include <memory>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/schema_registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

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

Http::FilterHeadersStatus AiProtocolManagerFilter::decodeHeaders(Http::RequestHeaderMap&,
                                                                 bool end_stream) {
  // A headers-only request carries no payload to inspect, so there is nothing to
  // hold the chain for: let the headers flow. (Pausing here would also deadlock,
  // since no body would ever arrive to drive the replay that releases them.)
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Copy what the route declared; see the note on schema_ for why the config is
  // not held by pointer.
  if (const RouteConfig* route_config =
          Http::Utility::resolveMostSpecificPerFilterConfig<RouteConfig>(decoder_callbacks_);
      route_config != nullptr) {
    schema_ = route_config->schema();
    normalize_ = route_config->normalize();
    ENVOY_LOG(debug, "ai_protocol_manager: route declares schema {}{}",
              PerRouteProto::Schema_Name(schema_), normalize_ ? ", normalizing" : "");
  }

  // A declared AI endpoint is parsed strictly. Any other route is parsed only if
  // the filter opted into best-effort parsing, and never fails the request.
  // TODO(penguingao): best-effort parsing takes the buffering path below for every bodied request
  // on every route, which deadlocks full-duplex requests (e.g. gRPC client/bidi streaming):
  // headers are held until the request's end_stream, which the client may not send until it
  // sees a response the upstream cannot produce. Gate the best-effort path on content-type
  // (only application/json and *+json; never gRPC or upgrades), and on a best-effort parse
  // failure release the held headers and buffered body immediately and pass the remainder
  // through unbuffered, rather than buffering to end-of-stream.
  if (!isAiEndpoint() && !config_->bestEffortParsing()) {
    // Nothing will look at this payload, so stay out of the way: offloading it
    // would cost a store round-trip and withhold the headers meanwhile, for
    // nothing. Decided once here; decode_manager_ being null carries it.
    ENVOY_LOG(trace, "ai_protocol_manager: route has no payload to inspect, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

  request_parser_ = std::make_unique<JsonWithExtBufParser>(JsonWithExtBufParser::Config{});
  // Built here, not at setDecoderFilterCallbacks(), so a pass-through stream pays
  // for none of it: constructing it subscribes to upstream watermarks and claims
  // a schedulable callback.
  decode_manager_ = std::make_unique<BufferManager>(
      buffer_factory_, std::make_unique<DecoderFilterChainBridge>(*decoder_callbacks_));

  // Pin the headers so routing and admission filters do not act on them before
  // the payload is offloaded. decodeData() still fires while iteration is stopped
  // here; the headers are released when replay injects the first body frame (or,
  // for an empty/trailer-only body, when the manager continues iteration).
  ENVOY_LOG(trace, "ai_protocol_manager: holding headers until payload is offloaded");
  return Http::FilterHeadersStatus::StopIteration;
}

bool AiProtocolManagerFilter::feedParser(const Buffer::Instance& data, bool end_stream) {
  // Feed the frame's slices where they lie: the parser accepts a split at any
  // byte boundary, so there is no need to join them into one block. Copying here
  // would reintroduce the per-stream footprint the external buffer avoids.
  absl::Status status = absl::OkStatus();
  const Buffer::RawSliceVector slices = data.getRawSlices();
  for (size_t i = 0; i < slices.size() && status.ok(); ++i) {
    // Only the final slice of a terminal frame closes the document.
    const bool last_slice = (i + 1 == slices.size());
    status = request_parser_->feed(
        absl::string_view(static_cast<const char*>(slices[i].mem_), slices[i].len_),
        last_slice && end_stream);
  }
  if (status.ok() && end_stream && slices.empty()) {
    // getRawSlices() omits empty slices, so a terminal frame carrying no bytes
    // leaves the document open; close it here.
    status = request_parser_->feed("", /*end_stream=*/true);
  }

  if (!status.ok()) {
    if (isAiEndpoint()) {
      rejectInvalidPayload(status);
      return false;
    }
    // Best effort: the payload is forwarded as it stands, just without a
    // document for later filters to work from.
    ENVOY_LOG(debug, "ai_protocol_manager: forwarding unparsed payload: {}", status.message());
    request_parser_.reset();
    return true;
  }

  if (end_stream) {
    request_json_ = request_parser_->takeDocument();
    request_parser_.reset();

    if (isAiEndpoint()) {
      // TODO(penguingao): Support validating payload schema on the fly as the Wuffs parser
      // streams and parses chunks, rejecting invalid fields early before end_stream.
      if (const PayloadSchema* payload_schema = SchemaRegistry::getSchema(schema_);
          payload_schema != nullptr) {
        const absl::Status validation_status = payload_schema->validateRequest(request_json_);
        if (!validation_status.ok()) {
          rejectInvalidPayload(validation_status);
          return false;
        }
      }
    }
  }
  return true;
}

void AiProtocolManagerFilter::rejectInvalidPayload(const absl::Status& status) {
  ENVOY_LOG(debug, "ai_protocol_manager: rejecting request: {}", status.message());
  payload_rejected_ = true;
  request_parser_.reset();
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, status.message(), nullptr,
                                     std::nullopt, "ai_protocol_manager_invalid_json");
}

Http::FilterDataStatus AiProtocolManagerFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (decode_manager_ == nullptr) {
    // decodeHeaders() decided this stream is none of our business.
    return Http::FilterDataStatus::Continue;
  }
  if (payload_rejected_) {
    // Already terminated by the local reply; drop whatever is still in flight.
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (request_parser_ != nullptr) {
    // The framing promised a body but no byte of one arrived -- a chunked request
    // whose only chunk is the terminator, or an empty terminal DATA frame. There
    // is no payload to validate, so the request passes through, the same as one
    // that ended on its headers. The parser has been fed nothing exactly when the
    // manager has been given nothing, since every frame reaches both.
    if (end_stream && data.length() == 0 && decode_manager_->empty()) {
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
  if (decode_manager_ == nullptr) {
    return Http::FilterTrailersStatus::Continue;
  }
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
