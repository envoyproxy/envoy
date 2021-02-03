#include "extensions/filters/http/grpc_http1_reverse_bridge/filter.h"

#include "envoy/http/header_map.h"

#include "common/common/enum_to_int.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/grpc/status.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    accept_handle(Http::CustomHeaders::get().Accept);

struct RcDetailsValues {
  // The gRPC HTTP/1 reverse bridge failed because the body payload was too
  // small to be a gRPC frame.
  const std::string GrpcBridgeFailedTooSmall = "grpc_bridge_data_too_small";
  // The gRPC HTTP/1 bridge encountered an unsupported content type.
  const std::string GrpcBridgeFailedContentType = "grpc_bridge_content_type_wrong";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

namespace {
Grpc::Status::GrpcStatus grpcStatusFromHeaders(Http::ResponseHeaderMap& headers) {
  const auto http_response_status = Http::Utility::getResponseStatus(headers);

  // Notably, we treat an upstream 200 as a successful response. This differs
  // from the standard but is key in being able to transform a successful
  // upstream HTTP response into a gRPC response.
  if (http_response_status == 200) {
    return Grpc::Status::WellKnownGrpcStatus::Ok;
  } else {
    return Grpc::Utility::httpToGrpcStatus(http_response_status);
  }
}

std::string badContentTypeMessage(const Http::ResponseHeaderMap& headers) {
  if (headers.ContentType() != nullptr) {
    return fmt::format(
        "envoy reverse bridge: upstream responded with unsupported content-type {}, status code {}",
        headers.getContentTypeValue(), headers.getStatusValue());
  } else {
    return fmt::format(
        "envoy reverse bridge: upstream responded with no content-type header, status code {}",
        headers.getStatusValue());
  }
}

void adjustContentLength(Http::RequestOrResponseHeaderMap& headers,
                         const std::function<uint64_t(uint64_t value)>& adjustment) {
  auto length_header = headers.getContentLengthValue();
  if (!length_header.empty()) {
    uint64_t length;
    if (absl::SimpleAtoi(length_header, &length)) {
      if (length != 0) {
        headers.setContentLength(adjustment(length));
      }
    }
  }
}
} // namespace

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  // Short circuit if header only.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Disable filter per route config if applies
  if (decoder_callbacks_->route() != nullptr) {
    const auto* per_route_config =
        Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(
            Extensions::HttpFilters::HttpFilterNames::get().GrpcHttp1ReverseBridge,
            decoder_callbacks_->route());
    if (per_route_config != nullptr && per_route_config->disabled()) {
      enabled_ = false;
      return Http::FilterHeadersStatus::Continue;
    }
  }

  // If this is a gRPC request we:
  //  - mark this request as being gRPC
  //  - change the content-type to application/x-protobuf
  if (Envoy::Grpc::Common::isGrpcRequestHeaders(headers)) {
    enabled_ = true;

    // We keep track of the original content-type to ensure that we handle
    // gRPC content type variations such as application/grpc+proto.
    content_type_ = std::string(headers.getContentTypeValue());
    headers.setContentType(upstream_content_type_);
    headers.setInline(accept_handle.handle(), upstream_content_type_);

    if (withhold_grpc_frames_) {
      // Adjust the content-length header to account for us removing the gRPC frame header.
      adjustContentLength(headers, [](auto size) { return size - Grpc::GRPC_FRAME_HEADER_SIZE; });
    }

    // Clear the route cache to recompute the cache. This provides additional
    // flexibility around request modification through the route table.
    decoder_callbacks_->clearRouteCache();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& buffer, bool) {
  if (enabled_ && withhold_grpc_frames_ && !prefix_stripped_) {
    // Fail the request if the body is too small to possibly contain a gRPC frame.
    if (buffer.length() < Grpc::GRPC_FRAME_HEADER_SIZE) {
      decoder_callbacks_->sendLocalReply(Http::Code::OK, "invalid request body", nullptr,
                                         Grpc::Status::WellKnownGrpcStatus::Unknown,
                                         RcDetails::get().GrpcBridgeFailedTooSmall);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Remove the gRPC frame header.
    buffer.drain(Grpc::GRPC_FRAME_HEADER_SIZE);
    prefix_stripped_ = true;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (enabled_) {
    absl::string_view content_type = headers.getContentTypeValue();

    // If the response from upstream does not have the correct content-type,
    // perform an early return with a useful error message in grpc-message.
    if (content_type != upstream_content_type_) {
      decoder_callbacks_->sendLocalReply(Http::Code::OK, badContentTypeMessage(headers), nullptr,
                                         Grpc::Status::WellKnownGrpcStatus::Unknown,
                                         RcDetails::get().GrpcBridgeFailedContentType);

      return Http::FilterHeadersStatus::StopIteration;
    }

    // Restore the content-type to match what the downstream sent.
    headers.setContentType(content_type_);

    if (withhold_grpc_frames_) {
      // Adjust content-length to account for the frame header that's added.
      adjustContentLength(headers,
                          [](auto length) { return length + Grpc::GRPC_FRAME_HEADER_SIZE; });
    }
    // We can only insert trailers at the end of data, so keep track of this value
    // until then.
    grpc_status_ = grpcStatusFromHeaders(headers);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& buffer, bool end_stream) {
  if (!enabled_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    // Insert grpc-status trailers to communicate the error code.
    auto& trailers = encoder_callbacks_->addEncodedTrailers();
    trailers.setGrpcStatus(grpc_status_);

    if (withhold_grpc_frames_) {
      buffer.prepend(buffer_);
      buildGrpcFrameHeader(buffer);
    }

    return Http::FilterDataStatus::Continue;
  }

  // We only need to buffer if we're responsible for injecting the gRPC frame header.
  if (withhold_grpc_frames_) {
    // Buffer the response in a mutable buffer: we need to determine the size of the response
    // and modify it later on.
    buffer_.move(buffer);
    return Http::FilterDataStatus::StopIterationAndBuffer;
  } else {
    return Http::FilterDataStatus::Continue;
  }
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (!enabled_) {
    return Http::FilterTrailersStatus::Continue;
  }

  trailers.setGrpcStatus(grpc_status_);

  if (withhold_grpc_frames_) {
    buildGrpcFrameHeader(buffer_);
    encoder_callbacks_->addEncodedData(buffer_, false);
  }

  return Http::FilterTrailersStatus::Continue;
}

void Filter::buildGrpcFrameHeader(Buffer::Instance& buffer) {
  // We do this even if the upstream failed: If the response returned non-200,
  // we'll respond with a grpc-status with an error, so clients will know that the request
  // was unsuccessful. Since we're guaranteed at this point to have a valid response
  // (unless upstream lied in content-type) we attempt to return a well-formed gRPC
  // response body.
  Grpc::Encoder().prependFrameHeader(Grpc::GRPC_FH_DEFAULT, buffer);
}

} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
