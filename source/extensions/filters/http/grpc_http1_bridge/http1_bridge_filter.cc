#include "extensions/filters/http/grpc_http1_bridge/http1_bridge_filter.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/grpc/context_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {

void Http1BridgeFilter::chargeStat(const Http::ResponseHeaderOrTrailerMap& headers) {
  context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, *request_stat_names_,
                      headers.GrpcStatus());
}

Http::FilterHeadersStatus Http1BridgeFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const bool grpc_request = Grpc::Common::isGrpcRequestHeaders(headers);
  if (grpc_request) {
    setupStatTracking(headers);
  }

  const absl::optional<Http::Protocol>& protocol = decoder_callbacks_->streamInfo().protocol();
  ASSERT(protocol);
  if (protocol.value() < Http::Protocol::Http2 && grpc_request) {
    do_bridging_ = true;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Http1BridgeFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                           bool end_stream) {
  if (doStatTracking()) {
    chargeStat(headers);
  }

  if (!do_bridging_) {
    return Http::FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;
  if (end_stream) {
    // We may still need to set an http status and content length based on gRPC trailers
    // present in the response headers. This is known as a gRPC trailers-only response.
    // If the grpc status is non-zero, this will change the response code.
    updateHttpStatusAndContentLength(headers);
    return Http::FilterHeadersStatus::Continue;
  } else {
    return Http::FilterHeadersStatus::StopIteration;
  }
}

Http::FilterDataStatus Http1BridgeFilter::encodeData(Buffer::Instance&, bool end_stream) {
  if (!do_bridging_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    return Http::FilterDataStatus::Continue;
  } else {
    // Buffer until the complete request has been processed.
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Http::FilterTrailersStatus Http1BridgeFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (doStatTracking()) {
    chargeStat(trailers);
  }

  if (do_bridging_) {
    // We're bridging, so we need to process trailers and set the http status, content length,
    // grpc status, and grpc message from those trailers.
    doResponseTrailers(trailers);
  }

  // NOTE: We will still write the trailers, but the HTTP/1.1 codec will just eat them and end
  //       the chunk encoded response which is what we want.
  return Http::FilterTrailersStatus::Continue;
}

void Http1BridgeFilter::updateHttpStatusAndContentLength(
    const Http::ResponseHeaderOrTrailerMap& trailers) {
  // Here we check for grpc-status. If it's not zero, we change the response code. We assume
  // that if a reset comes in and we disconnect the HTTP/1.1 client it will raise some type
  // of exception/error that the response was not complete.
  const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();
  if (grpc_status_header) {
    uint64_t grpc_status_code;
    if (!absl::SimpleAtoi(grpc_status_header->value().getStringView(), &grpc_status_code) ||
        grpc_status_code != 0) {
      response_headers_->setStatus(enumToInt(Http::Code::ServiceUnavailable));
    }
  }

  // Since we are buffering, set content-length so that HTTP/1.1 callers can better determine
  // if this is a complete response.
  response_headers_->setContentLength(
      encoder_callbacks_->encodingBuffer() ? encoder_callbacks_->encodingBuffer()->length() : 0);
}

// Set grpc response headers based on incoming trailers. Sometimes, the incoming
// trailers are in fact upstream headers, in the case of a gRPC trailers-only response.
void Http1BridgeFilter::updateGrpcStatusAndMessage(
    const Http::ResponseHeaderOrTrailerMap& trailers) {
  const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();
  if (grpc_status_header) {
    response_headers_->setGrpcStatus(grpc_status_header->value().getStringView());
  }

  const Http::HeaderEntry* grpc_message_header = trailers.GrpcMessage();
  if (grpc_message_header) {
    response_headers_->setGrpcMessage(grpc_message_header->value().getStringView());
  }
}

// Process response trailers. This involves setting an appropriate http status and content length,
// as well as gRPC status and message headers.
void Http1BridgeFilter::doResponseTrailers(const Http::ResponseHeaderOrTrailerMap& trailers) {
  // First we need to set an HTTP status based on the gRPC status in `trailers`.
  // We also set content length based on whether there exists an encoding buffer.
  updateHttpStatusAndContentLength(trailers);

  // Finally we set the grpc status and message headers based on `trailers`.
  updateGrpcStatusAndMessage(trailers);
}

void Http1BridgeFilter::setupStatTracking(const Http::RequestHeaderMap& headers) {
  cluster_ = decoder_callbacks_->clusterInfo();
  if (!cluster_) {
    return;
  }
  request_stat_names_ = context_.resolveDynamicServiceAndMethod(headers.Path());
}

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
