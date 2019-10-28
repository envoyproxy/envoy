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

struct RcDetailsValues {
  // The gRPC HTTP/1 reverse bridge failed because the body payload was too
  // small to be a gRPC frame.
  const std::string GrpcBridgeFailedTooSmall = "grpc_bridge_data_too_small";
  // The gRPC HTTP/1 bridge encountered an unsupported content type.
  const std::string GrpcBridgeFailedContentType = "grpc_bridge_content_type_wrong";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

namespace {
Grpc::Status::GrpcStatus grpcStatusFromHeaders(Http::HeaderMap& headers) {
  const auto http_response_status = Http::Utility::getResponseStatus(headers);

  // Notably, we treat an upstream 200 as a successful response. This differs
  // from the standard but is key in being able to transform a successful
  // upstream HTTP response into a gRPC response.
  if (http_response_status == 200) {
    return Grpc::Status::GrpcStatus::Ok;
  } else {
    return Grpc::Utility::httpToGrpcStatus(http_response_status);
  }
}

std::string badContentTypeMessage(const Http::HeaderMap& headers) {
  if (headers.ContentType() != nullptr) {
    return fmt::format(
        "envoy reverse bridge: upstream responded with unsupported content-type {}, status code {}",
        headers.ContentType()->value().getStringView(), headers.Status()->value().getStringView());
  } else {
    return fmt::format(
        "envoy reverse bridge: upstream responded with no content-type header, status code {}",
        headers.Status()->value().getStringView());
  }
}

void adjustContentLength(Http::HeaderMap& headers,
                         const std::function<uint64_t(uint64_t value)>& adjustment) {
  auto length_header = headers.ContentLength();
  if (length_header != nullptr) {
    uint64_t length;
    if (absl::SimpleAtoi(length_header->value().getStringView(), &length)) {
      length_header->value(adjustment(length));
    }
  }
}
} // namespace

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
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
  if (Envoy::Grpc::Common::hasGrpcContentType(headers)) {
    enabled_ = true;

    // We keep track of the original content-type to ensure that we handle
    // gRPC content type variations such as application/grpc+proto.
    content_type_ = std::string(headers.ContentType()->value().getStringView());
    headers.ContentType()->value(upstream_content_type_);
    headers.insertAccept().value(upstream_content_type_);

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
                                         Grpc::Status::GrpcStatus::Unknown,
                                         RcDetails::get().GrpcBridgeFailedTooSmall);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Remove the gRPC frame header.
    buffer.drain(Grpc::GRPC_FRAME_HEADER_SIZE);
    prefix_stripped_ = true;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (enabled_) {
    auto content_type = headers.ContentType();

    // If the response from upstream does not have the correct content-type,
    // perform an early return with a useful error message in grpc-message.
    if (content_type == nullptr ||
        content_type->value().getStringView() != upstream_content_type_) {
      headers.insertGrpcMessage().value(badContentTypeMessage(headers));
      headers.insertGrpcStatus().value(Envoy::Grpc::Status::GrpcStatus::Unknown);
      headers.insertStatus().value(enumToInt(Http::Code::OK));

      if (content_type != nullptr) {
        content_type->value(content_type_);
      }

      decoder_callbacks_->streamInfo().setResponseCodeDetails(
          RcDetails::get().GrpcBridgeFailedContentType);
      return Http::FilterHeadersStatus::ContinueAndEndStream;
    }

    // Restore the content-type to match what the downstream sent.
    content_type->value(content_type_);

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
    trailers.insertGrpcStatus().value(grpc_status_);

    if (withhold_grpc_frames_) {
      // Compute the size of the payload and construct the length prefix.
      //
      // We do this even if the upstream failed: If the response returned non-200,
      // we'll respond with a grpc-status with an error, so clients will know that the request
      // was unsuccessful. Since we're guaranteed at this point to have a valid response
      // (unless upstream lied in content-type) we attempt to return a well-formed gRPC
      // response body.
      const auto length = buffer.length() + buffer_.length();

      std::array<uint8_t, Grpc::GRPC_FRAME_HEADER_SIZE> frame;
      Grpc::Encoder().newFrame(Grpc::GRPC_FH_DEFAULT, length, frame);

      buffer.prepend(buffer_);
      Buffer::OwnedImpl frame_buffer(frame.data(), frame.size());
      buffer.prepend(frame_buffer);
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

} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
