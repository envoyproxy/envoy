#include "source/extensions/filters/http/connect_grpc_bridge/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/extensions/filters/http/connect_grpc_bridge/end_stream_response.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

namespace {

struct RcDetailsValues {
  // The Buf Connect gRPC bridge tried to buffer a unary request that was too large for the
  // configured decoder buffer limit.
  const std::string ConnectBridgeUnaryRequestTooLarge = "connect_bridge_unary_request_too_large";
  // The Buf Connect gRPC bridge tried to buffer a unary response that was too large for the
  // configured encoder buffer limit.
  const std::string ConnectBridgeUnaryResponseTooLarge = "connect_bridge_unary_response_too_large";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

struct GrpcTimeoutSuffixesValues {
  const std::string MillisecondsSuffix = "m";
  const std::string SecondsSuffix = "S";
  const std::string MinutesSuffix = "M";
  const std::string HoursSuffix = "H";
};
using GrpcTimeoutSuffixes = ConstSingleton<GrpcTimeoutSuffixesValues>;

struct ConnectHeaderPartsValues {
  const Http::LowerCaseString TrailerHeader{"trailer"};
  const Http::LowerCaseString GrpcHeaderPrefix{"grpc-"};
  const std::string TrailerHeaderPrefix = "trailer-";
  const std::string UnaryContentTypePrefix = "application/";
};
using ConnectHeaderParts = ConstSingleton<ConnectHeaderPartsValues>;

constexpr uint64_t GRPC_MAX_TIMEOUT_INTEGRAL = 9999'9999;

void renameHeader(Http::HeaderMap& headers, const Envoy::Http::LowerCaseString& from,
                  const Envoy::Http::LowerCaseString& to) {
  auto old_headers = headers.get(from);
  for (size_t index = 0; index < old_headers.size(); index++) {
    Http::HeaderString key{to};
    Http::HeaderString value;
    value.setCopy(old_headers[index]->value().getStringView());
    headers.addViaMove(std::move(key), std::move(value));
  }
  headers.remove(from);
}

absl::optional<std::string> formatGrpcTimeout(uint64_t timeout_ms) {
  if (timeout_ms <= GRPC_MAX_TIMEOUT_INTEGRAL) {
    return absl::StrCat(timeout_ms, GrpcTimeoutSuffixes::get().MillisecondsSuffix);
  }

  uint64_t timeout_s = timeout_ms / 1000;
  if (timeout_s <= GRPC_MAX_TIMEOUT_INTEGRAL) {
    return absl::StrCat(timeout_s, GrpcTimeoutSuffixes::get().SecondsSuffix);
  }

  uint64_t timeout_m = timeout_s / 60;
  if (timeout_m <= GRPC_MAX_TIMEOUT_INTEGRAL) {
    return absl::StrCat(timeout_m, GrpcTimeoutSuffixes::get().MinutesSuffix);
  }

  uint64_t timeout_h = timeout_m / 60;
  if (timeout_h <= GRPC_MAX_TIMEOUT_INTEGRAL) {
    return absl::StrCat(timeout_h, GrpcTimeoutSuffixes::get().HoursSuffix);
  }

  return {};
}

void convertConnectTimeoutToGrpcTimeout(Http::HeaderMap& headers) {
  auto timeout_headers = headers.get(Http::CustomHeaders::get().ConnectTimeoutMs);

  if (timeout_headers.empty()) {
    return;
  }

  auto last_timeout_header = timeout_headers[timeout_headers.size() - 1];

  uint64_t timeout_ms;

  if (!absl::SimpleAtoi(last_timeout_header->value().getStringView(), &timeout_ms)) {
    return;
  }

  auto grpc_timeout_value = formatGrpcTimeout(timeout_ms);
  if (!grpc_timeout_value.has_value()) {
    return;
  }

  Http::HeaderString key{Http::CustomHeaders::get().GrpcTimeout};
  Http::HeaderString value;
  value.setCopy(*grpc_timeout_value);
  headers.addViaMove(std::move(key), std::move(value));
  headers.remove(Http::CustomHeaders::get().ConnectTimeoutMs);
}

// We're using a template because we need both HeaderMap and
// HeaderOrTrailerResponseMap, but they're not related types.
template <typename T>
Buffer::OwnedImpl convertGrpcResponseToConnectStreamingResponse(const T& headers) {
  EndStreamResponse connect_response;
  const auto grpc_status = Grpc::Common::getGrpcStatus(headers);

  // If status is 0, we don't write an error at all.
  if (!grpc_status || *grpc_status != 0) {
    // TODO: Should handle error details, as per
    //       https://connect.build/docs/protocol#error-end-stream
    Error error;
    error.code = *grpc_status;
    error.message = Grpc::Common::getGrpcMessage(headers);

    connect_response.error.emplace(error);
  }

  // Convert trailers to metadata.
  auto& metadata = connect_response.metadata;
  headers.iterate([&metadata](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    if (!absl::StartsWith(header.key().getStringView(),
                          ConnectHeaderParts::get().GrpcHeaderPrefix)) {
      if (auto it = metadata.find(header.key().getStringView()); it != metadata.end()) {
        it->second.emplace_back(header.value().getStringView());
      } else {
        metadata.insert(std::make_pair<std::string, std::vector<std::string>>(
            std::string{header.key().getStringView()},
            std::vector{std::string{header.value().getStringView()}}));
      }
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  // Send converted metadata encapsulated in a Connect frame.
  Buffer::OwnedImpl buffer;
  serializeJson(connect_response, buffer);
  Grpc::Encoder().prependFrameHeader(Grpc::CONNECT_FH_EOS, buffer);
  return buffer;
}

template <typename T>
absl::optional<Buffer::OwnedImpl>
convertGrpcResponseToConnectUnaryResponse(Http::ResponseHeaderMap& response_headers,
                                          const T& trailers) {
  absl::optional<Buffer::OwnedImpl> encoded_error;

  response_headers.remove(ConnectHeaderParts::get().TrailerHeader);

  auto status = Grpc::Common::getGrpcStatus(trailers);
  if (status && *status != Grpc::Status::Ok) {
    // TODO: Should handle error details, as per
    //       https://connect.build/docs/protocol#error-end-stream
    Error error;
    error.code = *status;
    error.message = Grpc::Common::getGrpcMessage(trailers);

    response_headers.setStatus(statusCodeToConnectUnaryStatus(error.code));
    response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
    Buffer::OwnedImpl buffer;
    serializeJson(error, buffer);
    encoded_error = std::move(buffer);
  }

  trailers.iterate([&response_headers](const Http::HeaderEntry& trailer) {
    if (!absl::StartsWith(trailer.key().getStringView(),
                          ConnectHeaderParts::get().GrpcHeaderPrefix)) {
      response_headers.addCopy(
          Http::LowerCaseString{absl::StrCat(ConnectHeaderParts::get().TrailerHeaderPrefix,
                                             trailer.key().getStringView())},
          trailer.value().getStringView());
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  return encoded_error;
}

} // namespace

// Unary filter

ConnectUnaryToGrpcFilter::ConnectUnaryToGrpcFilter() = default;

Http::FilterHeadersStatus ConnectUnaryToGrpcFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                  bool) {
  if (headers.get(Http::CustomHeaders::get().ConnectProtocolVersion).empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  std::string content_type{headers.getContentTypeValue()};

  // Filter out streaming connect requests.
  // Connect unary content-types always start with "application/".
  if (absl::StartsWith(content_type, Http::Headers::get().ContentTypeValues.Connect) ||
      !absl::StartsWith(content_type, ConnectHeaderParts::get().UnaryContentTypePrefix)) {
    return Http::FilterHeadersStatus::Continue;
  }

  is_connect_unary_ = true;

  // Rewrite content-type header.
  content_type =
      absl::StrCat(Http::Headers::get().ContentTypeValues.Grpc, "+",
                   content_type.substr(ConnectHeaderParts::get().UnaryContentTypePrefix.size()));
  headers.setContentType(content_type);

  // Convert connect timeout to gRPC timeout.
  convertConnectTimeoutToGrpcTimeout(headers);

  // Remove connect protocol header.
  headers.remove(Http::CustomHeaders::get().ConnectProtocolVersion);
  headers.remove(Http::CustomHeaders::get().AcceptEncoding);

  headers.setTE(Http::Headers::get().TEValues.Trailers);
  headers.addReferenceKey(Http::CustomHeaders::get().AcceptEncoding,
                          Http::CustomHeaders::get().AcceptEncodingValues.Identity);
  headers.addReferenceKey(Http::CustomHeaders::get().GrpcAcceptEncoding,
                          Http::CustomHeaders::get().AcceptEncodingValues.Identity);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ConnectUnaryToGrpcFilter::decodeData(Buffer::Instance& data,
                                                            bool end_stream) {
  if (is_connect_unary_) {
    response_buffer_.move(data);
    if (decoderBufferLimitReached(response_buffer_.length())) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    if (!end_stream) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }

    Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT, response_buffer_);
    data.move(response_buffer_);
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus ConnectUnaryToGrpcFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (is_connect_unary_) {
    Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT, response_buffer_);
    decoder_callbacks_->addDecodedData(response_buffer_, true);
  }

  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus ConnectUnaryToGrpcFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                  bool end_stream) {
  if (!is_connect_unary_ || !Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    return Http::FilterHeadersStatus::Continue;
  }

  is_grpc_response_ = true;
  response_headers_ = &headers;

  if (end_stream) {
    // Handle trailers-only responses.
    auto err_buf = convertGrpcResponseToConnectUnaryResponse(headers, headers);

    if (err_buf.has_value()) {
      encoder_callbacks_->addEncodedData(*err_buf, true);
    }

    return Http::FilterHeadersStatus::Continue;
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus ConnectUnaryToGrpcFilter::encodeData(Buffer::Instance& data,
                                                            bool end_stream) {
  if (is_grpc_response_) {
    if (!drained_frame_header_) {
      data.drain(Envoy::Grpc::GRPC_FRAME_HEADER_SIZE);
      drained_frame_header_ = true;
    }

    request_buffer_.move(data);
    if (encoderBufferLimitReached(request_buffer_.length())) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    if (!end_stream) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }

    data.move(request_buffer_);

    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
ConnectUnaryToGrpcFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (!is_grpc_response_) {
    return Http::FilterTrailersStatus::Continue;
  }

  ASSERT(response_headers_ != nullptr);

  auto err_buf = convertGrpcResponseToConnectUnaryResponse(*response_headers_, trailers);

  if (err_buf.has_value()) {
    encoder_callbacks_->addEncodedData(*err_buf, true);
  } else {
    encoder_callbacks_->addEncodedData(request_buffer_, true);
  }

  trailers.clear();

  return Http::FilterTrailersStatus::Continue;
}

bool ConnectUnaryToGrpcFilter::decoderBufferLimitReached(uint64_t buffer_length) {
  if (decoder_callbacks_->decoderBufferLimit() > 0 &&
      buffer_length > decoder_callbacks_->decoderBufferLimit()) {
    ENVOY_STREAM_LOG(debug,
                     "Request rejected because the filter's internal buffer size exceeds the "
                     "configured limit: {} > {}",
                     *decoder_callbacks_, buffer_length, decoder_callbacks_->decoderBufferLimit());
    decoder_callbacks_->sendLocalReply(
        Http::Code::PayloadTooLarge,
        "Request rejected because the filter's internal buffer size exceeds the configured limit.",
        nullptr, absl::nullopt, RcDetails::get().ConnectBridgeUnaryRequestTooLarge);
    return true;
  }
  return false;
}

bool ConnectUnaryToGrpcFilter::encoderBufferLimitReached(uint64_t buffer_length) {
  if (encoder_callbacks_->encoderBufferLimit() > 0 &&
      buffer_length > encoder_callbacks_->encoderBufferLimit()) {
    ENVOY_STREAM_LOG(
        debug,
        "Response discarded because the filter's internal buffer size exceeds the configured "
        "limit: {} > {}",
        *encoder_callbacks_, buffer_length, encoder_callbacks_->encoderBufferLimit());
    encoder_callbacks_->sendLocalReply(
        Http::Code::InternalServerError,
        "Response discarded because the filter's internal buffer size exceeds the configured "
        "limit.",
        nullptr, absl::nullopt, RcDetails::get().ConnectBridgeUnaryResponseTooLarge);
    return true;
  }
  return false;
}

// Streaming filter

ConnectStreamingToGrpcFilter::ConnectStreamingToGrpcFilter() = default;

Http::FilterHeadersStatus
ConnectStreamingToGrpcFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  std::string content_type{headers.getContentTypeValue()};

  if (!absl::StartsWith(content_type, Http::Headers::get().ContentTypeValues.Connect) ||
      content_type.size() <= Http::Headers::get().ContentTypeValues.Connect.size() ||
      content_type[Http::Headers::get().ContentTypeValues.Connect.size()] != '+') {
    return Http::FilterHeadersStatus::Continue;
  }

  is_connect_streaming_ = true;

  content_type =
      absl::StrCat(Http::Headers::get().ContentTypeValues.Grpc, "+",
                   content_type.substr(Http::Headers::get().ContentTypeValues.Connect.size() + 1));
  headers.setContentType(content_type);

  renameHeader(headers, Http::CustomHeaders::get().ConnectContentEncoding,
               Http::CustomHeaders::get().GrpcEncoding);
  renameHeader(headers, Http::CustomHeaders::get().ConnectAcceptEncoding,
               Http::CustomHeaders::get().GrpcAcceptEncoding);

  // Convert connect timeout to gRPC timeout.
  convertConnectTimeoutToGrpcTimeout(headers);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus
ConnectStreamingToGrpcFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  if (!is_connect_streaming_ || !Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Convert from gRPC response to Connect response.
  is_grpc_response_ = true;

  // Rewrite Content-Type header.
  std::string content_type{headers.getContentTypeValue()};
  if (absl::StartsWith(content_type, Http::Headers::get().ContentTypeValues.Grpc) &&
      content_type.size() > Http::Headers::get().ContentTypeValues.Grpc.size() &&
      content_type[Http::Headers::get().ContentTypeValues.Grpc.size()] == '+') {
    content_type =
        absl::StrCat(Http::Headers::get().ContentTypeValues.Connect, "+",
                     content_type.substr(Http::Headers::get().ContentTypeValues.Grpc.size() + 1));
  } else {
    // Default to proto if no suffix is present on gRPC content-type.
    content_type = Http::Headers::get().ContentTypeValues.ConnectProto;
  }
  headers.setContentType(content_type);

  // Rename compatible headers.
  renameHeader(headers, Http::CustomHeaders::get().GrpcEncoding,
               Http::CustomHeaders::get().ConnectContentEncoding);
  renameHeader(headers, Http::CustomHeaders::get().GrpcAcceptEncoding,
               Http::CustomHeaders::get().ConnectAcceptEncoding);

  // Remove connect protocol header.
  headers.remove(Http::CustomHeaders::get().ConnectProtocolVersion);

  if (end_stream) {
    // Handle trailers-only responses.
    auto response = convertGrpcResponseToConnectStreamingResponse(headers);
    encoder_callbacks_->addEncodedData(response, true);
    headers.removePrefix(Http::LowerCaseString{ConnectHeaderParts::get().GrpcHeaderPrefix});
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus
ConnectStreamingToGrpcFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (!is_grpc_response_) {
    return Http::FilterTrailersStatus::Continue;
  }

  auto response = convertGrpcResponseToConnectStreamingResponse(trailers);
  encoder_callbacks_->addEncodedData(response, true);
  trailers.clear();

  return Http::FilterTrailersStatus::Continue;
}

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
