#include "source/extensions/filters/http/connect_grpc_bridge/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/extensions/filters/http/connect_grpc_bridge/end_stream_response.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

namespace {

struct RcDetailsValues {
  // The Connect RPC to gRPC bridge tried to buffer a unary request that was too large for the
  // configured decoder buffer limit.
  const std::string ConnectBridgeUnaryRequestTooLarge = "connect_bridge_unary_request_too_large";
  // The Connect RPC to gRPC bridge tried to buffer a unary response that was too large for the
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

struct ConnectGetParamValues {
  const std::string EncodingKey = "encoding";
  const std::string MessageKey = "message";
  const std::string Base64Key = "base64";
  const std::string CompressionKey = "compression";
  const std::string APIKey = "connect";
  const std::string APIValue = "v1";
};
using ConnectGetParams = ConstSingleton<ConnectGetParamValues>;

struct ConnectHeaderPartsValues {
  const Http::LowerCaseString TrailerHeader{"trailer"};
  const Http::LowerCaseString GrpcHeaderPrefix{"grpc-"};
  const std::string TrailerHeaderPrefix = "trailer-";
  const std::string UnaryContentTypePrefix = "application/";
  const std::string UnaryProtobufContentType = "application/proto";
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
Buffer::OwnedImpl convertGrpcResponseToConnectStreamingResponse(const T& headers,
                                                                bool use_metadata) {
  EndStreamResponse connect_response;
  const auto grpc_status = Grpc::Common::getGrpcStatus(headers);

  // If status is 0, we don't write an error at all.
  if (!grpc_status || *grpc_status != 0) {
    Error error;
    error.code = *grpc_status;
    error.message = Grpc::Common::getGrpcMessage(headers);

    if (const auto status_bin = Grpc::Common::getGrpcStatusDetailsBin(headers)) {
      for (const auto& detail : status_bin->details()) {
        error.details.push_back(detail);
      }
    }

    connect_response.error.emplace(error);
  }

  // Convert trailers to metadata.
  if (use_metadata) {
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
  }

  // Send converted metadata encapsulated in a Connect frame.
  Buffer::OwnedImpl buffer;
  std::string tmp;
  if (serializeJson(connect_response, tmp)) {
    buffer.add(tmp);
  } else {
    buffer.add("{}");
  }
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
    Error error;
    error.code = *status;
    error.message = Grpc::Common::getGrpcMessage(trailers);

    if (const auto status_bin = Grpc::Common::getGrpcStatusDetailsBin(trailers)) {
      for (const auto& detail : status_bin->details()) {
        error.details.push_back(detail);
      }
    }

    response_headers.setStatus(statusCodeToConnectUnaryStatus(error.code));
    response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
    response_headers.removeContentLength();

    std::string tmp;
    if (serializeJson(error, tmp)) {
      encoded_error.emplace(tmp);
    }
  }

  return encoded_error;
}

void convertGrpcTrailersToConnectHeaders(Http::ResponseHeaderMap& response_headers,
                                         const Http::ResponseTrailerMap& trailers) {
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
}

absl::string_view removeQueryParameters(const absl::string_view path) {
  absl::string_view result = path;
  if (size_t offset = result.find('?'); offset != absl::string_view::npos) {
    result.remove_suffix(result.length() - offset);
  }
  return result;
}

} // namespace

ConnectGrpcBridgeFilter::ConnectGrpcBridgeFilter() = default;

Http::FilterHeadersStatus ConnectGrpcBridgeFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                 bool end_stream) {
  std::string content_type{headers.getContentTypeValue()};

  if (absl::StartsWith(content_type, Http::Headers::get().ContentTypeValues.Connect) &&
      content_type.size() > Http::Headers::get().ContentTypeValues.Connect.size() &&
      content_type[Http::Headers::get().ContentTypeValues.Connect.size()] == '+') {
    // Streaming Connect protocol
    is_connect_streaming_ = true;

    // Rewrite content-type header.
    content_type = absl::StrCat(
        Http::Headers::get().ContentTypeValues.Grpc, "+",
        content_type.substr(Http::Headers::get().ContentTypeValues.Connect.size() + 1));
    headers.setContentType(content_type);

    // Convert connect timeout to gRPC timeout.
    convertConnectTimeoutToGrpcTimeout(headers);

    // Remove connect protocol header.
    headers.remove(Http::CustomHeaders::get().ConnectProtocolVersion);

    headers.setTE(Http::Headers::get().TEValues.Trailers);
    renameHeader(headers, Http::CustomHeaders::get().ConnectContentEncoding,
                 Http::CustomHeaders::get().GrpcEncoding);
    renameHeader(headers, Http::CustomHeaders::get().ConnectAcceptEncoding,
                 Http::CustomHeaders::get().GrpcAcceptEncoding);

    // Remove content-length, if it's present; our length will differ.
    headers.removeContentLength();
  } else if (!headers.get(Http::CustomHeaders::get().ConnectProtocolVersion).empty() &&
             absl::StartsWith(content_type, ConnectHeaderParts::get().UnaryContentTypePrefix)) {
    // Unary Connect protocol
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

    headers.setTE(Http::Headers::get().TEValues.Trailers);
    renameHeader(headers, Http::CustomHeaders::get().ContentEncoding,
                 Http::CustomHeaders::get().GrpcEncoding);
    renameHeader(headers, Http::CustomHeaders::get().AcceptEncoding,
                 Http::CustomHeaders::get().GrpcAcceptEncoding);

    auto compression = headers.get(Http::CustomHeaders::get().GrpcEncoding);
    unary_payload_frame_flags_ = Envoy::Grpc::GRPC_FH_DEFAULT;
    if (!compression.empty() &&
        compression[0]->value() != Http::CustomHeaders::get().AcceptEncodingValues.Identity) {
      unary_payload_frame_flags_ |= Envoy::Grpc::GRPC_FH_COMPRESSED;
    }

    headers.removeContentLength();

    if (end_stream) {
      Grpc::Encoder().prependFrameHeader(unary_payload_frame_flags_, request_buffer_);
      decoder_callbacks_->addDecodedData(request_buffer_, true);
    }
  } else {
    Http::Utility::QueryParamsMulti query_parameters =
        Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
    if (query_parameters.getFirstValue(ConnectGetParams::get().APIKey).value_or("") ==
        ConnectGetParams::get().APIValue) {
      // Unary Connect Get protocol
      is_connect_unary_ = true;

      headers.setMethod(Http::Headers::get().MethodValues.Post);
      headers.setPath(removeQueryParameters(headers.getPathValue()));

      auto message =
          query_parameters.getFirstValue(ConnectGetParams::get().MessageKey).value_or("");
      auto base64 = query_parameters.getFirstValue(ConnectGetParams::get().Base64Key).value_or("");
      auto encoding =
          query_parameters.getFirstValue(ConnectGetParams::get().EncodingKey).value_or("");
      auto compression =
          query_parameters.getFirstValue(ConnectGetParams::get().CompressionKey).value_or("");

      if (base64 == "1") {
        message = Base64Url::decode(message);
      }

      if (compression.empty()) {
        compression = Http::CustomHeaders::get().AcceptEncodingValues.Identity;
      }

      unary_payload_frame_flags_ = Envoy::Grpc::GRPC_FH_DEFAULT;
      if (compression != Http::CustomHeaders::get().AcceptEncodingValues.Identity) {
        unary_payload_frame_flags_ |= Envoy::Grpc::GRPC_FH_COMPRESSED;
      }

      // Set content-type header.
      content_type = absl::StrCat(Http::Headers::get().ContentTypeValues.Grpc, "+", encoding);
      headers.setContentType(content_type);

      // Convert connect timeout to gRPC timeout.
      convertConnectTimeoutToGrpcTimeout(headers);

      headers.setTE(Http::Headers::get().TEValues.Trailers);
      headers.addReferenceKey(Http::CustomHeaders::get().GrpcEncoding, compression);
      renameHeader(headers, Http::CustomHeaders::get().AcceptEncoding,
                   Http::CustomHeaders::get().GrpcAcceptEncoding);

      request_buffer_.add(message);
      if (decoderBufferLimitReached(request_buffer_.length())) {
        return Http::FilterHeadersStatus::StopIteration;
      }
      headers.removeContentLength();

      if (end_stream) {
        Grpc::Encoder().prependFrameHeader(unary_payload_frame_flags_, request_buffer_);
        decoder_callbacks_->addDecodedData(request_buffer_, true);
      }
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ConnectGrpcBridgeFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (is_connect_unary_) {
    request_buffer_.move(data);
    if (decoderBufferLimitReached(request_buffer_.length())) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    if (!end_stream) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }

    Grpc::Encoder().prependFrameHeader(unary_payload_frame_flags_, request_buffer_);
    data.move(request_buffer_);
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus ConnectGrpcBridgeFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (is_connect_unary_) {
    Grpc::Encoder().prependFrameHeader(unary_payload_frame_flags_, request_buffer_);
    decoder_callbacks_->addDecodedData(request_buffer_, true);
  }

  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus ConnectGrpcBridgeFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                 bool end_stream) {
  response_headers_ = &headers;

  if (is_connect_streaming_ || is_connect_unary_) {
    if (!Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
      return Http::FilterHeadersStatus::Continue;
    }

    is_grpc_response_ = true;

    // Our response will always differ in size.
    headers.removeContentLength();
  }

  if (is_connect_streaming_) {
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
      auto response = convertGrpcResponseToConnectStreamingResponse(headers, false);
      encoder_callbacks_->addEncodedData(response, true);
      headers.removePrefix(Http::LowerCaseString{ConnectHeaderParts::get().GrpcHeaderPrefix});
    }
  } else if (is_connect_unary_) {
    // Rewrite Content-Type header.
    std::string content_type{headers.getContentTypeValue()};
    if (absl::StartsWith(content_type, Http::Headers::get().ContentTypeValues.Grpc) &&
        content_type.size() > Http::Headers::get().ContentTypeValues.Grpc.size() &&
        content_type[Http::Headers::get().ContentTypeValues.Grpc.size()] == '+') {
      content_type =
          absl::StrCat(ConnectHeaderParts::get().UnaryContentTypePrefix,
                       content_type.substr(Http::Headers::get().ContentTypeValues.Grpc.size() + 1));
    } else {
      // Default to proto if no suffix is present on gRPC content-type.
      content_type = ConnectHeaderParts::get().UnaryProtobufContentType;
    }
    headers.setContentType(content_type);

    // Handle content coding.
    renameHeader(headers, Http::CustomHeaders::get().GrpcEncoding,
                 Http::CustomHeaders::get().ContentEncoding);

    if (!end_stream) {
      return Http::FilterHeadersStatus::StopIteration;
    }

    // Handle trailers-only responses.
    auto err_buf = convertGrpcResponseToConnectUnaryResponse(headers, headers);

    if (err_buf.has_value()) {
      response_headers_->remove(Http::CustomHeaders::get().ContentEncoding);
      encoder_callbacks_->addEncodedData(*err_buf, true);
    }

    headers.removePrefix(Http::LowerCaseString{ConnectHeaderParts::get().GrpcHeaderPrefix});
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ConnectGrpcBridgeFilter::encodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (is_grpc_response_ && is_connect_unary_) {
    // Handle flag byte. If uncompressed, we need to remove the content encoding.
    if (drained_frame_header_bytes_ == 0 && data.length() > 0) {
      auto flags = data.peekInt<uint8_t>();
      if ((flags & Grpc::GRPC_FH_COMPRESSED) == 0) {
        response_headers_->remove(Http::CustomHeaders::get().ContentEncoding);
      }
    }
    if (drained_frame_header_bytes_ < Envoy::Grpc::GRPC_FRAME_HEADER_SIZE) {
      uint64_t to_drain = std::min(
          Envoy::Grpc::GRPC_FRAME_HEADER_SIZE - drained_frame_header_bytes_, data.length());
      data.drain(to_drain);
      drained_frame_header_bytes_ += to_drain;
    }

    response_buffer_.move(data);
    if (encoderBufferLimitReached(response_buffer_.length())) {
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    if (!end_stream) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }

    data.move(response_buffer_);
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
ConnectGrpcBridgeFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (!is_grpc_response_) {
    return Http::FilterTrailersStatus::Continue;
  }

  if (is_connect_streaming_) {
    auto response = convertGrpcResponseToConnectStreamingResponse(trailers, true);
    encoder_callbacks_->addEncodedData(response, true);
    trailers.clear();
  } else if (is_connect_unary_) {
    ASSERT(response_headers_ != nullptr);

    convertGrpcTrailersToConnectHeaders(*response_headers_, trailers);

    auto err_buf = convertGrpcResponseToConnectUnaryResponse(*response_headers_, trailers);

    if (err_buf.has_value()) {
      response_headers_->remove(Http::CustomHeaders::get().ContentEncoding);
      encoder_callbacks_->addEncodedData(*err_buf, true);
    } else {
      encoder_callbacks_->addEncodedData(response_buffer_, true);
    }

    trailers.clear();
  }

  return Http::FilterTrailersStatus::Continue;
}

bool ConnectGrpcBridgeFilter::decoderBufferLimitReached(uint64_t buffer_length) {
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

bool ConnectGrpcBridgeFilter::encoderBufferLimitReached(uint64_t buffer_length) {
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

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
