#include "source/extensions/filters/http/grpc_web/grpc_web_filter.h"

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

namespace {

// This is the maximum buffered plaintext data length when we have buffered data in the encoding
// buffer. This is effectively used (to limit the length of grpc-message) only when we have encoding
// buffer filled with data. The value is arbitrarily chosen. This can be made configurable when it
// is required.
constexpr uint64_t MAX_BUFFERED_PLAINTEXT_LENGTH = 16384;

// This builds grpc-message header value from body data.
std::string buildGrpcMessage(Buffer::Instance& body_data) {
  const uint64_t message_length = body_data.length();
  std::string message;
  message.reserve(message_length);
  message.resize(message_length);
  body_data.copyOut(0, message_length, message.data());

  return Http::Utility::PercentEncoding::encode(message);
}

} // namespace

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    accept_handle(Http::CustomHeaders::get().Accept);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    grpc_accept_encoding_handle(Http::CustomHeaders::get().GrpcAcceptEncoding);

struct RcDetailsValues {
  // The grpc web filter couldn't decode the data as the size wasn't a multiple of 4.
  const std::string GrpcDecodeFailedDueToSize = "grpc_base_64_decode_failed_bad_size";
  // The grpc web filter couldn't decode the data provided.
  const std::string GrpcDecodeFailedDueToData = "grpc_base_64_decode_failed";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

// Bit mask denotes a trailers frame of gRPC-Web.
const uint8_t GrpcWebFilter::GRPC_WEB_TRAILER = 0b10000000;

// Supported gRPC-Web content-types.
const absl::flat_hash_set<std::string>& GrpcWebFilter::gRpcWebContentTypes() const {
  static const absl::flat_hash_set<std::string>* types = new absl::flat_hash_set<std::string>(
      {Http::Headers::get().ContentTypeValues.GrpcWeb,
       Http::Headers::get().ContentTypeValues.GrpcWebProto,
       Http::Headers::get().ContentTypeValues.GrpcWebText,
       Http::Headers::get().ContentTypeValues.GrpcWebTextProto});
  return *types;
}

bool GrpcWebFilter::isGrpcWebRequest(const Http::RequestHeaderMap& headers) {
  if (!headers.Path()) {
    return false;
  }
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type != nullptr) {
    return gRpcWebContentTypes().count(content_type->value().getStringView()) > 0;
  }
  return false;
}

bool GrpcWebFilter::isProtoEncodedGrpcWebResponseHeaders(
    const Http::ResponseHeaderMap& headers) const {
  // We expect the response headers to have 200 OK status (a valid gRPC, also gRPC-Web, response
  // needs to have 200 OK status
  // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#responses) and contain
  // proto-encoded gRPC-Web content-type.
  return Http::Utility::getResponseStatus(headers) == enumToInt(Http::Code::OK) &&
         hasProtoEncodedGrpcWebContentType(headers);
}

// TODO(dio): Move this as a shared utility function.
bool GrpcWebFilter::hasProtoEncodedGrpcWebContentType(
    const Http::RequestOrResponseHeaderMap& headers) const {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type != nullptr) {
    absl::string_view content_type_value = content_type->value().getStringView();
    // We ignore "parameter" value. Note that "*( ";" parameter )" indicates that there can be
    // multiple parameters.
    absl::string_view current_content_type =
        StringUtil::rtrim(content_type_value.substr(0, content_type_value.find_first_of(';')));
    // We expect only proto encoding response
    // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md. And the value of media-type is
    // case-sensitive https://tools.ietf.org/html/rfc2616#section-3.7.
    return StringUtil::CaseInsensitiveCompare()(
               current_content_type, Http::Headers::get().ContentTypeValues.GrpcWebProto) ||
           StringUtil::CaseInsensitiveCompare()(current_content_type,
                                                Http::Headers::get().ContentTypeValues.GrpcWeb);
  }
  return false;
}

// If response headers do not contain valid response headers, it needs transformation.
bool GrpcWebFilter::needsTransformationForNonProtoEncodedResponse(Http::ResponseHeaderMap& headers,
                                                                  bool end_stream) const {
  // We transform the response unless it is already a gRPC or proto-encoded gRPC-Web response.
  return !Grpc::Common::isGrpcResponseHeaders(headers, end_stream) &&
         !isProtoEncodedGrpcWebResponseHeaders(headers);
}

void GrpcWebFilter::mergeAndLimitNonProtoEncodedResponseData(Buffer::OwnedImpl& output,
                                                             Buffer::Instance* last_data) {
  const auto* encoding_buffer = encoder_callbacks_->encodingBuffer();
  if (encoding_buffer != nullptr) {
    if (last_data != nullptr) {
      encoder_callbacks_->addEncodedData(*last_data, false);
    }
    encoder_callbacks_->modifyEncodingBuffer([&output](Buffer::Instance& buffered) {
      // When we have buffered data (encoding buffer is filled), we limit the final buffer length.
      output.move(buffered, MAX_BUFFERED_PLAINTEXT_LENGTH);
      buffered.drain(buffered.length());
    });
  } else if (last_data != nullptr) {
    // In the case of local reply and when the response only contains a single data chunk,
    // "encoding_buffer" is nullptr and we only have filled "last_data".
    output.move(*last_data);
    last_data->drain(last_data->length());
  }
}

void GrpcWebFilter::setTransformedNonProtoEncodedResponseHeaders(Buffer::Instance* data) {
  Buffer::OwnedImpl merged_data;
  // When we have buffered data in encoding buffer, we limit the length of the output to be smaller
  // than MAX_BUFFERED_PLAINTEXT_LENGTH. However, when we only have "last" data, we send it all.
  mergeAndLimitNonProtoEncodedResponseData(merged_data, data);

  const std::string grpc_message = buildGrpcMessage(merged_data);
  response_headers_->setGrpcMessage(grpc_message);
  response_headers_->setGrpcStatus(Grpc::Utility::httpToGrpcStatus(
      enumToInt(Http::Utility::getResponseStatus(*response_headers_))));
  response_headers_->setContentLength(0);
}

// Implements StreamDecoderFilter.
// TODO(fengli): Implements the subtypes of gRPC-Web content-type other than proto, like +json, etc.
Http::FilterHeadersStatus GrpcWebFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (!isGrpcWebRequest(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }
  is_grpc_web_request_ = true;

  // Remove content-length header since it represents http1.1 payload size, not the sum of the h2
  // DATA frame payload lengths. https://http2.github.io/http2-spec/#malformed This effectively
  // switches to chunked encoding which is the default for h2
  headers.removeContentLength();
  setupStatTracking(headers);

  const absl::string_view content_type = headers.getContentTypeValue();
  if (content_type == Http::Headers::get().ContentTypeValues.GrpcWebText ||
      content_type == Http::Headers::get().ContentTypeValues.GrpcWebTextProto) {
    // Checks whether gRPC-Web client is sending base64 encoded request.
    is_text_request_ = true;
  }
  headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);

  const absl::string_view accept = headers.getInlineValue(accept_handle.handle());
  if (accept == Http::Headers::get().ContentTypeValues.GrpcWebText ||
      accept == Http::Headers::get().ContentTypeValues.GrpcWebTextProto) {
    // Checks whether gRPC-Web client is asking for base64 encoded response.
    is_text_response_ = true;
  }

  // Adds te:trailers to upstream HTTP2 request. It's required for gRPC.
  headers.setReferenceTE(Http::Headers::get().TEValues.Trailers);
  if (headers.get(Http::CustomHeaders::get().GrpcAcceptEncoding).empty()) {
    // Adds grpc-accept-encoding:identity
    headers.setReferenceInline(grpc_accept_encoding_handle.handle(),
                               Http::CustomHeaders::get().GrpcAcceptEncodingValues.Default);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GrpcWebFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!is_grpc_web_request_) {
    return Http::FilterDataStatus::Continue;
  }

  if (!is_text_request_) {
    // No additional transcoding required if gRPC client is sending binary request.
    return Http::FilterDataStatus::Continue;
  }

  // Parse application/grpc-web-text format.
  const uint64_t available = data.length() + decoding_buffer_.length();
  if (end_stream) {
    if (available == 0) {
      return Http::FilterDataStatus::Continue;
    }
    if (available % 4 != 0) {
      // Client end stream with invalid base64. Note, base64 padding is mandatory.
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                         "Bad gRPC-web request, invalid base64 data.", nullptr,
                                         absl::nullopt, RcDetails::get().GrpcDecodeFailedDueToSize);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  } else if (available < 4) {
    decoding_buffer_.move(data);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  const uint64_t needed = available / 4 * 4 - decoding_buffer_.length();
  decoding_buffer_.move(data, needed);
  const std::string decoded = Base64::decode(
      std::string(static_cast<const char*>(decoding_buffer_.linearize(decoding_buffer_.length())),
                  decoding_buffer_.length()));
  if (decoded.empty()) {
    // Error happened when decoding base64.
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "Bad gRPC-web request, invalid base64 data.", nullptr,
                                       absl::nullopt, RcDetails::get().GrpcDecodeFailedDueToData);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  decoding_buffer_.drain(decoding_buffer_.length());
  decoding_buffer_.move(data);
  data.add(decoded);
  // Any block of 4 bytes or more should have been decoded and passed through.
  ASSERT(decoding_buffer_.length() < 4);
  return Http::FilterDataStatus::Continue;
}

// Implements StreamEncoderFilter.
Http::FilterHeadersStatus GrpcWebFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                       bool end_stream) {
  if (!is_grpc_web_request_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (doStatTracking()) {
    chargeStat(headers);
  }

  needs_transformation_for_non_proto_encoded_response_ =
      needsTransformationForNonProtoEncodedResponse(headers, end_stream);

  if (is_text_response_) {
    headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.GrpcWebTextProto);
  } else {
    headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.GrpcWebProto);
  }

  if (end_stream || !needs_transformation_for_non_proto_encoded_response_) {
    return Http::FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus GrpcWebFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!is_grpc_web_request_) {
    return Http::FilterDataStatus::Continue;
  }

  // When the upstream response (this is also relevant for local reply, since gRPC-Web request is
  // not a gRPC request which makes the local reply's is_grpc_request set to false) is not a gRPC
  // response, we set the "grpc-message" header with the upstream body content.
  if (needs_transformation_for_non_proto_encoded_response_) {
    const auto* encoding_buffer = encoder_callbacks_->encodingBuffer();
    if (!end_stream) {
      // We limit the buffered data in encoding buffer here to eliminate the possibility of
      // buffering too large data from upstream. Note that the buffered data here will be
      // transformed as grpc-message later.
      if (encoding_buffer != nullptr &&
          encoding_buffer->length() >= MAX_BUFFERED_PLAINTEXT_LENGTH) {
        return Http::FilterDataStatus::StopIterationNoBuffer;
      }
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }

    ASSERT(response_headers_ != nullptr);
    needs_transformation_for_non_proto_encoded_response_ = false;
    setTransformedNonProtoEncodedResponseHeaders(&data);
    return Http::FilterDataStatus::Continue;
  }

  if (!is_text_response_) {
    // No additional transcoding required if gRPC-Web client asked for binary response.
    return Http::FilterDataStatus::Continue;
  }

  // The decoder always consumes and drains the given buffer. Incomplete data frame is buffered
  // inside the decoder.
  std::vector<Grpc::Frame> frames;
  std::ignore = decoder_.decode(data, frames);
  if (frames.empty()) {
    // We don't have enough data to decode for one single frame, stop iteration until more data
    // comes in.
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  // Encodes the decoded gRPC frames with base64.
  for (auto& frame : frames) {
    Buffer::OwnedImpl temp;
    temp.add(&frame.flags_, 1);
    const uint32_t length = htonl(frame.length_);
    temp.add(&length, 4);
    if (frame.length_ > 0) {
      temp.add(*frame.data_);
    }
    data.add(Base64::encode(temp, temp.length()));
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus GrpcWebFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (!is_grpc_web_request_) {
    return Http::FilterTrailersStatus::Continue;
  }

  if (doStatTracking()) {
    chargeStat(trailers);
  }

  if (needs_transformation_for_non_proto_encoded_response_) {
    setTransformedNonProtoEncodedResponseHeaders(nullptr);
    return Http::FilterTrailersStatus::Continue;
  }

  // Trailers are expected to come all in once, and will be encoded into one single trailers frame.
  // Trailers in the trailers frame are separated by `CRLFs`.
  Buffer::OwnedImpl temp;
  trailers.iterate([&temp](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    temp.add(header.key().getStringView().data(), header.key().size());
    temp.add(":");
    temp.add(header.value().getStringView().data(), header.value().size());
    temp.add("\r\n");
    return Http::HeaderMap::Iterate::Continue;
  });

  // Clears out the trailers so they don't get added since it is now in the body.
  trailers.clear();
  Buffer::OwnedImpl buffer;
  // Adds the trailers frame head.
  buffer.add(&GRPC_WEB_TRAILER, 1);
  // Adds the trailers frame length.
  const uint32_t length = htonl(temp.length());
  buffer.add(&length, 4);
  buffer.move(temp);
  if (is_text_response_) {
    Buffer::OwnedImpl encoded(Base64::encode(buffer, buffer.length()));
    encoder_callbacks_->addEncodedData(encoded, true);
  } else {
    encoder_callbacks_->addEncodedData(buffer, true);
  }
  return Http::FilterTrailersStatus::Continue;
}

void GrpcWebFilter::setupStatTracking(const Http::RequestHeaderMap& headers) {
  cluster_ = decoder_callbacks_->clusterInfo();
  if (!cluster_) {
    return;
  }
  request_stat_names_ = context_.resolveDynamicServiceAndMethod(headers.Path());
}

void GrpcWebFilter::chargeStat(const Http::ResponseHeaderOrTrailerMap& headers) {
  context_.chargeStat(*cluster_, Grpc::Context::Protocol::GrpcWeb, *request_stat_names_,
                      headers.GrpcStatus());
}

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
