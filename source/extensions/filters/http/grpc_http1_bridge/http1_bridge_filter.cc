#include "source/extensions/filters/http/grpc_http1_bridge/http1_bridge_filter.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"

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
    setupHttp1Status(headers.GrpcStatus(), headers.GrpcMessage());
    return Http::FilterHeadersStatus::Continue;
  } else {
    return Http::FilterHeadersStatus::StopIteration;
  }
}

Http::FilterDataStatus Http1BridgeFilter::encodeData(Buffer::Instance&, bool end_stream) {
  if (!do_bridging_){
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    setupHttp1Status(response_headers_->GrpcStatus(), response_headers_->GrpcMessage());
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
    setupHttp1Status(trailers.GrpcStatus(), trailers.GrpcMessage());
  }

  // NOTE: We will still write the trailers, but the HTTP/1.1 codec will just eat them and end
  //       the chunk encoded response which is what we want.
  return Http::FilterTrailersStatus::Continue;
}

void Http1BridgeFilter::setupStatTracking(const Http::RequestHeaderMap& headers) {
  cluster_ = decoder_callbacks_->clusterInfo();
  if (!cluster_) {
    return;
  }
  request_stat_names_ = context_.resolveDynamicServiceAndMethod(headers.Path());
}

void Http1BridgeFilter::setupHttp1Status(const Http::HeaderEntry* grpc_status, const Http::HeaderEntry* grpc_message){
  // Here we check for grpc-status. If it's not zero, we change the response code. We assume
  // that if a reset comes in and we disconnect the HTTP/1.1 client it will raise some type
  // of exception/error that the response was not complete.
  if (grpc_status) {
    uint64_t grpc_status_code;
    if (!absl::SimpleAtoi(grpc_status->value().getStringView(), &grpc_status_code) ||
      grpc_status_code != 0) {
      response_headers_->setStatus(enumToInt(Http::Code::ServiceUnavailable));
    }
      response_headers_->setGrpcStatus(grpc_status->value().getStringView());
  }

  if (grpc_message) {
    response_headers_->setGrpcMessage(grpc_message->value().getStringView());
  }

  // Since we are buffering, set content-length so that HTTP/1.1 callers can better determine
  // if this is a complete response.
  response_headers_->setContentLength(
  encoder_callbacks_->encodingBuffer() ? encoder_callbacks_->encodingBuffer()->length() : 0);
}

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
