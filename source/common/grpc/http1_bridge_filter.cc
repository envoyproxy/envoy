#include "common.h"
#include "http1_bridge_filter.h"

#include "envoy/http/codes.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"

namespace Grpc {

void Http1BridgeFilter::chargeStat(const Http::HeaderMap& headers) {
  const Http::HeaderEntry* grpc_status_header = headers.GrpcStatus();
  if (!grpc_status_header) {
    return;
  }

  uint64_t grpc_status_code;
  bool success = StringUtil::atoul(grpc_status_header->value().c_str(), grpc_status_code) &&
                 grpc_status_code == 0;

  Common::chargeStat(stats_store_, cluster_, grpc_service_, grpc_method_, success);
}

Http::FilterHeadersStatus Http1BridgeFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  bool grpc_request = content_type && content_type->value() == Common::GRPC_CONTENT_TYPE.c_str();
  if (grpc_request) {
    setupStatTracking(headers);
  }

  if (decoder_callbacks_->requestInfo().protocol() != Http::Protocol::Http2 && grpc_request) {
    do_bridging_ = true;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Http1BridgeFilter::encodeHeaders(Http::HeaderMap& headers,
                                                           bool end_stream) {
  if (do_stat_tracking_) {
    chargeStat(headers);
  }

  if (!do_bridging_ || end_stream) {
    return Http::FilterHeadersStatus::Continue;
  } else {
    response_headers_ = &headers;
    return Http::FilterHeadersStatus::StopIteration;
  }
}

Http::FilterDataStatus Http1BridgeFilter::encodeData(Buffer::Instance&, bool end_stream) {
  if (!do_bridging_ || end_stream) {
    return Http::FilterDataStatus::Continue;
  } else {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Http::FilterTrailersStatus Http1BridgeFilter::encodeTrailers(Http::HeaderMap& trailers) {
  if (do_stat_tracking_) {
    chargeStat(trailers);
  }

  if (do_bridging_) {
    // Here we check for grpc-status. If it's not zero, we change the response code. We assume
    // that if a reset comes in and we disconnect the HTTP/1.1 client it will raise some type
    // of exception/error that the response was not complete.
    const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();
    if (grpc_status_header) {
      uint64_t grpc_status_code;
      if (!StringUtil::atoul(grpc_status_header->value().c_str(), grpc_status_code) ||
          grpc_status_code != 0) {
        response_headers_->Status()->value(enumToInt(Http::Code::ServiceUnavailable));
      }
      response_headers_->insertGrpcStatus().value(*grpc_status_header);
    }

    const Http::HeaderEntry* grpc_message_header = trailers.GrpcMessage();
    if (grpc_message_header) {
      response_headers_->insertGrpcMessage().value(*grpc_message_header);
    }

    // Since we are buffering, set content-length so that HTTP/1.1 callers can better determine
    // if this is a complete response.
    response_headers_->insertContentLength().value(
        encoder_callbacks_->encodingBuffer() ? encoder_callbacks_->encodingBuffer()->length() : 0);
  }

  // NOTE: We will still write the trailers, but the HTTP/1.1 codec will just eat them and end
  //       the chunk encoded response which is what we want.
  return Http::FilterTrailersStatus::Continue;
}

void Http1BridgeFilter::setupStatTracking(const Http::HeaderMap& headers) {
  const Router::RouteEntry* route = decoder_callbacks_->routeTable().routeForRequest(headers);
  if (!route) {
    return;
  }

  std::vector<std::string> parts = StringUtil::split(headers.Path()->value().c_str(), '/');
  if (parts.size() != 2) {
    return;
  }

  cluster_ = route->clusterName();
  grpc_service_ = parts[0];
  grpc_method_ = parts[1];
  do_stat_tracking_ = true;
}

} // Grpc
