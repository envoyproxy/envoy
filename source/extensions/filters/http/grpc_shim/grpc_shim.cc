#include "extensions/filters/http/grpc_shim/grpc_shim.h"

#include <netinet/in.h>

#include "envoy/common/platform.h"

#include "common/common/enum_to_int.h"
#include "common/grpc/common.h"
#include "common/grpc/codec.h"
#include "common/grpc/status.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/str_cat.h"

namespace {
Envoy::Grpc::Status::GrpcStatus grpcStatusFromHeaders(Envoy::Http::HeaderMap& headers) {
  const auto http_response_status = Envoy::Http::Utility::getResponseStatus(headers);

  // Notably, we treat an upstream 200 as a successful response. This differs
  // from the standard but is key in being able to transform a successful
  // upstream HTTP response into a gRPC response.
  if (http_response_status == 200) {
    return Envoy::Grpc::Status::GrpcStatus::Ok;
  } else {
    return Envoy::Grpc::Utility::httpToGrpcStatus(http_response_status);
  }
}
} // namespace

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcShim {

Http::FilterHeadersStatus GrpcShim::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  // Short circuit if header only.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // TODO(snowp): Add an enabled flag so that this filter can be enabled on a per route basis.

  // If this is a gRPC request we:
  //  - mark this request as being gRPC
  //  - change the content-type to application/x-protobuf
  if (Envoy::Grpc::Common::hasGrpcContentType(headers)) {
    enabled_ = true;

    // We keep track of the original content-type to ensure that we handle
    // gRPC content type variations such as application/grpc+proto.
    content_type_ = headers.ContentType()->value().c_str();
    headers.ContentType()->value(upstream_content_type_);
    headers.insertAccept().value(upstream_content_type_);

    // Clear the route cache to recompute the cache. This provides additional
    // flexibility around request modification through the route table.
    decoder_callbacks_->clearRouteCache();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GrpcShim::decodeData(Buffer::Instance& buffer, bool) {
  // If this is a gRPC request and this is the start of DATA, remove the first 5
  // bytes. These correspond to the gRPC data header.
  if (enabled_ && !prefix_stripped_) {
    buffer.drain(5);
    prefix_stripped_ = true;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus GrpcShim::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (enabled_) {
    auto content_type = headers.ContentType();

    // If the response from upstream does not have the correct content-type,
    // perform an early return with a useful error message in grpc-message.
    if (content_type->value().getStringView() != upstream_content_type_) {
      const auto grpc_message = fmt::format(
          "envoy grpc-shim: upstream responded with unsupported content-type {}, status code {}",
          content_type->value().getStringView(), headers.Status()->value().getStringView());

      headers.insertGrpcMessage().value(grpc_message);
      headers.insertGrpcStatus().value(Envoy::Grpc::Status::GrpcStatus::Unknown);
      headers.insertStatus().value(enumToInt(Http::Code::OK));
      content_type->value(content_type_);

      return Http::FilterHeadersStatus::ContinueAndEndStream;
    }

    // Restore the content-type to match what the downstream sent.
    content_type->value(content_type_);

    // We can only insert trailers at the end of data, so keep track of this value
    // until then.
    grpc_status_ = grpcStatusFromHeaders(headers);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GrpcShim::encodeData(Buffer::Instance& buffer, bool end_stream) {
  if (!enabled_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    // Insert grpc-status trailers to communicate the error code.
    auto& trailers = encoder_callbacks_->addEncodedTrailers();
    trailers.insertGrpcStatus().value(grpc_status_);

    // Compute the size of the payload and construct the length prefix.
    //
    // We do this even if the upstream failed: If the response returned non-200,
    // we'll respond with a grpc-status with an error, so clients will know that the request
    // was unsuccessful. Since we're guaranteed at this point to have a valid response
    // (unless upstream lied in content-type) we attempt to return a well-formed gRPC
    // response body.
    const auto length = htonl(buffer.length() + buffer_.length());

    std::array<uint8_t, 5> frame;
    Grpc::Encoder().newFrame(Grpc::GRPC_FH_DEFAULT, htonl(length), frame);

    Buffer::OwnedImpl prefix_buffer;
    prefix_buffer.add(frame.data(), 5);
    prefix_buffer.move(buffer_);
    prefix_buffer.move(buffer);

    buffer.move(prefix_buffer);

    return Http::FilterDataStatus::Continue;
  }

  // Buffer the response in a mutable buffer: we need to determine the size of the response
  // and modify it later on.
  buffer_.move(buffer);
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

} // namespace GrpcShim
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
