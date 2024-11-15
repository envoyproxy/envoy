#include "source/extensions/filters/network/generic_proxy/codecs/http1/config.h"

#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Http1 {

static constexpr absl::string_view CRLF = "\r\n";
// Last chunk as defined here https://tools.ietf.org/html/rfc7230#section-4.1
static constexpr absl::string_view LAST_CHUNK = "0\r\n";

static constexpr absl::string_view SPACE = " ";
static constexpr absl::string_view COLON_SPACE = ": ";

static constexpr absl::string_view REQUEST_POSTFIX = " HTTP/1.1\r\n";
static constexpr absl::string_view RESPONSE_PREFIX = "HTTP/1.1 ";

static constexpr absl::string_view HOST_HEADER_PREFIX = ":a";

static constexpr uint32_t DEFAULT_MAX_BUFFER_SIZE = 8 * 1024 * 1024;

static constexpr absl::string_view _100_CONTINUE_RESPONSE = "HTTP/1.1 100 Continue\r\n"
                                                            "content-length: 0\r\n"
                                                            "\r\n";
static constexpr absl::string_view ZERO_VIEW = "0";

void encodeNormalHeaders(Buffer::Instance& buffer, const Http::RequestOrResponseHeaderMap& headers,
                         bool chunk_encoding) {
  headers.iterate([&buffer](const Http::HeaderEntry& header) {
    absl::string_view key_to_use = header.key().getStringView();
    // Translate :authority -> host so that upper layers do not need to deal with this.
    if (absl::StartsWith(key_to_use, HOST_HEADER_PREFIX)) {
      key_to_use = Http::Headers::get().HostLegacy;
    }

    // Skip all headers starting with ':' that make it here.
    if (key_to_use[0] == ':') {
      return Http::HeaderMap::Iterate::Continue;
    }

    buffer.addFragments({key_to_use, COLON_SPACE, header.value().getStringView(), CRLF});
    return Http::HeaderMap::Iterate::Continue;
  });

  if (chunk_encoding) {
    if (headers.TransferEncoding() == nullptr) {
      buffer.add("transfer-encoding: chunked\r\n");
    }
  }
}

absl::Status validateCommonHeaders(const Http::RequestOrResponseHeaderMap& headers) {
  // Both Transfer-Encoding and Content-Length are set.
  if (headers.TransferEncoding() != nullptr && headers.ContentLength() != nullptr) {
    return absl::InvalidArgumentError("Both transfer-encoding and content-length are set");
  }

  // Transfer-Encoding is not chunked.
  if (headers.TransferEncoding() != nullptr) {
    absl::string_view encoding = headers.TransferEncoding()->value().getStringView();
    if (!absl::EqualsIgnoreCase(encoding, Http::Headers::get().TransferEncodingValues.Chunked)) {
      return absl::InvalidArgumentError("transfer-encoding is not chunked");
    }
  }

  return absl::OkStatus();
}

bool Utility::isChunked(const Http::RequestOrResponseHeaderMap& headers, bool bodiless) {
  // If the message has body and the content length is not set, then treat it as chunked.
  // Note all upgrade and connect requests are rejected so this is safe.
  return !bodiless && headers.ContentLength() == nullptr;
}

bool Utility::hasBody(const Envoy::Http::Http1::Parser& parser, bool response,
                      bool response_for_head_request) {
  // Response for HEAD request should not have body.
  if (response_for_head_request) {
    ASSERT(response);
    return false;
  }

  // 1xx, 204, 304 responses should not have body.
  if (response) {
    const Envoy::Http::Code code = parser.statusCode();
    if (code < Http::Code::OK || code == Http::Code::NoContent || code == Http::Code::NotModified) {
      return false;
    }
  }

  // Check the transfer-encoding and content-length headers in other cases.
  return parser.isChunked() || parser.contentLength().value_or(0) > 0;
}

absl::Status Utility::validateRequestHeaders(Http::RequestHeaderMap& headers) {
  // No upgrade and connect support for now.
  // TODO(wbpcode): add support for upgrade and connect in the future.
  if (Http::Utility::isUpgrade(headers) ||
      headers.getMethodValue() == Http::Headers::get().MethodValues.Connect) {
    return absl::InvalidArgumentError("upgrade or connect are not supported");
  }

  if (auto status = validateCommonHeaders(headers); !status.ok()) {
    return status;
  }

  // One of method, path, host is missing.
  if (headers.Method() == nullptr || headers.Path() == nullptr || headers.Host() == nullptr) {
    return absl::InvalidArgumentError("missing required headers");
  }

  return absl::OkStatus();
}

absl::Status Utility::validateResponseHeaders(Http::ResponseHeaderMap& headers, Http::Code code) {
  if (auto status = validateCommonHeaders(headers); !status.ok()) {
    return status;
  }

  ASSERT(headers.Status() != nullptr);

  if (code < Http::Code::OK || code == Http::Code::NoContent || code == Http::Code::NotModified) {
    // 1xx, 204 responses should not have transfer-encoding. See
    // https://datatracker.ietf.org/doc/html/rfc9112#section-6.1-6.
    // There is no clear description in the RFC about the transfer-encoding behavior of NotModified
    // response. But it is safe to treat it as same as 1xx and 204 responses.
    if (headers.TransferEncoding() != nullptr) {
      return absl::InvalidArgumentError("transfer-encoding is set for 1xx/204/304 response");
    }

    // 1xx, 204, 304 responses should not have body.
    const auto content_length = headers.ContentLength();
    if (content_length != nullptr && content_length->value().getStringView() != ZERO_VIEW) {
      return absl::InvalidArgumentError("non-zero content-length is set for 1xx/204/304 response");
    }
  }

  return absl::OkStatus();
}

absl::Status Utility::encodeRequestHeaders(Buffer::Instance& buffer,
                                           const Http::RequestHeaderMap& headers,
                                           bool chunk_encoding) {
  const Http::HeaderEntry* method = headers.Method();
  const Http::HeaderEntry* path = headers.Path();
  const Http::HeaderEntry* host = headers.Host();

  if (method == nullptr || path == nullptr || host == nullptr) {
    return absl::InvalidArgumentError("missing required headers");
  }

  absl::string_view host_or_path_view = path->value().getStringView();

  buffer.addFragments({method->value().getStringView(), SPACE, host_or_path_view, REQUEST_POSTFIX});
  encodeNormalHeaders(buffer, headers, chunk_encoding);
  buffer.add(CRLF);

  return absl::OkStatus();
}

uint64_t Utility::statusToHttpStatus(absl::StatusCode status_code) {
  switch (status_code) {
  case absl::StatusCode::kOk:
    return 200;
  case absl::StatusCode::kCancelled:
    return 499;
  case absl::StatusCode::kUnknown:
    // Internal server error.
    return 500;
  case absl::StatusCode::kInvalidArgument:
    // Bad request.
    return 400;
  case absl::StatusCode::kDeadlineExceeded:
    // Gateway Time-out.
    return 504;
  case absl::StatusCode::kNotFound:
    // Not found.
    return 404;
  case absl::StatusCode::kAlreadyExists:
    // Conflict.
    return 409;
  case absl::StatusCode::kPermissionDenied:
    // Forbidden.
    return 403;
  case absl::StatusCode::kResourceExhausted:
    //  Too many requests.
    return 429;
  case absl::StatusCode::kFailedPrecondition:
    // Bad request.
    return 400;
  case absl::StatusCode::kAborted:
    // Conflict.
    return 409;
  case absl::StatusCode::kOutOfRange:
    // Bad request.
    return 400;
  case absl::StatusCode::kUnimplemented:
    // Not implemented.
    return 501;
  case absl::StatusCode::kInternal:
    // Internal server error.
    return 500;
  case absl::StatusCode::kUnavailable:
    // Service unavailable.
    return 503;
  case absl::StatusCode::kDataLoss:
    // Internal server error.
    return 500;
  case absl::StatusCode::kUnauthenticated:
    // Unauthorized.
    return 401;
  default:
    // Internal server error.
    return 500;
  }
}

absl::Status Utility::encodeResponseHeaders(Buffer::Instance& buffer,
                                            const Http::ResponseHeaderMap& headers,
                                            bool chunk_encoding) {
  const Http::HeaderEntry* status = headers.Status();
  if (status == nullptr) {
    return absl::InvalidArgumentError("missing required headers");
  }
  uint64_t numeric_status = Http::Utility::getResponseStatus(headers);

  absl::string_view reason_phrase;
  const char* status_string = Http::CodeUtility::toString(static_cast<Http::Code>(numeric_status));
  uint32_t status_string_len = strlen(status_string);
  reason_phrase = {status_string, status_string_len};

  buffer.addFragments({RESPONSE_PREFIX, absl::StrCat(numeric_status), SPACE, reason_phrase, CRLF});
  encodeNormalHeaders(buffer, headers, chunk_encoding);
  buffer.add(CRLF);

  return absl::OkStatus();
}

void Utility::encodeBody(Buffer::Instance& dst_buffer, Buffer::Instance& src_buffer,
                         bool chunk_encoding, bool end_stream) {
  if (src_buffer.length() > 0) {
    // Chunk header.
    if (chunk_encoding) {
      dst_buffer.add(absl::StrCat(absl::Hex(src_buffer.length()), CRLF));
    }

    // Body.
    dst_buffer.move(src_buffer);

    // Chunk footer.
    if (chunk_encoding) {
      dst_buffer.add(CRLF);
    }
  }

  // Add additional LAST_CHUNK if this is the last frame and chunk encoding is enabled.
  if (end_stream) {
    if (chunk_encoding) {
      dst_buffer.addFragments({LAST_CHUNK, CRLF});
    }
  }
}

bool Http1CodecBase::decodeBuffer(Buffer::Instance& buffer) {
  decoding_buffer_.move(buffer);

  // Always resume before decoding.
  parser_->resume();

  while (decoding_buffer_.length() > 0) {
    const auto slice = decoding_buffer_.frontSlice();
    const auto nread = parser_->execute(static_cast<const char*>(slice.mem_), slice.len_);
    decoding_buffer_.drain(nread);

    if (const auto status = parser_->getStatus(); status == Http::Http1::ParserStatus::Error) {
      // Parser has encountered an error. Return false to indicate decoding failure. Ignore the
      // buffered body.
      return false;
    } else if (status == Http::Http1::ParserStatus::Paused || nread == 0) {
      // If parser is paused by the callback. Do nothing and return. Don't handle the buffered body
      // because parser is paused and no callback should be called.
      // If consumed buffer size is 0 means no more data to read, break to avoid infinite loop. This
      // is preventive check. The parser should not be in this state in normal cases.
      return true;
    }
  }

  // Try to dispatch any buffered body. We assume the headers should be handled in the callbacks.
  // And if the headers are not handled then the buffered body should be empty, this should be a
  // no-op. And if the message is complete then this will also be a no-op.
  return dispatchBufferedBody(false);
}

bool Http1CodecBase::dispatchBufferedBody(bool end_stream) {
  if (single_frame_mode_) {
    // Do nothing to the buffered body until the onMessageComplete callback if we are in single
    // frame mode.

    // Check if the buffered body is too large.
    if (bufferedBodyOverflow()) {
      // Pause the parser to avoid further parsing.
      parser_->pause();
      // Tell the caller that the decoding failed.
      return false;
    }
    return true;
  }

  if (buffered_body_.length() > 0 || end_stream) {
    ENVOY_LOG(debug, "Generic proxy HTTP1 codec: decoding message body (end_stream={} size={})",
              end_stream, buffered_body_.length());
    auto frame = std::make_unique<HttpRawBodyFrame>(buffered_body_, end_stream);
    onDecodingSuccess(std::move(frame));
  }

  return true;
}

bool Http1CodecBase::bufferedBodyOverflow() {
  if (buffered_body_.length() < max_buffer_size_) {
    return false;
  }

  ENVOY_LOG(warn, "Generic proxy HTTP1 codec: body size exceeds max size({} vs {})",
            buffered_body_.length(), max_buffer_size_);
  return true;
}

Http::Http1::CallbackResult Http1ServerCodec::onMessageBeginImpl() {
  if (active_request_.has_value()) {
    ENVOY_LOG(error,
              "Generic proxy HTTP1 codec: multiple requests on the same connection at same time.");
    return Http::Http1::CallbackResult::Error;
  }
  active_request_ = ActiveRequest{};

  active_request_->request_headers_ = Http::RequestHeaderMapImpl::create();
  return Http::Http1::CallbackResult::Success;
}

Http::Http1::CallbackResult Http1ServerCodec::onHeadersCompleteImpl() {
  if (!parser_->isHttp11()) {
    ENVOY_LOG(error,
              "Generic proxy HTTP1 codec: unsupported HTTP version, only HTTP/1.1 is supported.");
    return Http::Http1::CallbackResult::Error;
  }

  active_request_->request_headers_->setMethod(parser_->methodName());

  // Validate request headers.
  const auto validate_headers_status =
      Utility::validateRequestHeaders(*active_request_->request_headers_);
  if (!validate_headers_status.ok()) {
    ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to validate request headers: {}",
              validate_headers_status.message());
    return Http::Http1::CallbackResult::Error;
  }

  const bool non_end_stream = Utility::hasBody(*parser_, false, false);
  active_request_->request_has_body_ = non_end_stream;
  ENVOY_LOG(debug, "decoding request headers complete (end_stream={}):\n{}", !non_end_stream,
            *active_request_->request_headers_);

  // Handle the Expect header first.
  if (active_request_->request_headers_->Expect() != nullptr) {
    if (absl::EqualsIgnoreCase(active_request_->request_headers_->getExpectValue(),
                               Envoy::Http::Headers::get().ExpectValues._100Continue)) {
      // Remove the expect header then the upstream server won't handle it again.
      active_request_->request_headers_->removeExpect();

      // Send 100 Continue response directly. We won't proxy the 100 Continue because
      // the complexity in the generic proxy framework is too high.
      Buffer::OwnedImpl buffer(_100_CONTINUE_RESPONSE);
      callbacks_->writeToConnection(buffer);
    }
  }

  if (single_frame_mode_) {
    // Do nothing until the onMessageComplete callback if we are in single frame mode.
    return Http::Http1::CallbackResult::Success;
  } else if (non_end_stream) {
    auto request =
        std::make_unique<HttpRequestFrame>(std::move(active_request_->request_headers_), false);
    onDecodingSuccess(std::move(request), {});
  } else {
    deferred_end_stream_headers_ = true;
  }

  return Http::Http1::CallbackResult::Success;
}

Http::Http1::CallbackResult Http1ServerCodec::onMessageCompleteImpl() {
  active_request_->request_complete_ = true;

  if (single_frame_mode_) {
    // Check if the buffered body is too large.
    if (bufferedBodyOverflow()) {
      // Pause the parser to avoid further parsing.
      parser_->pause();
      // Tell the caller that the decoding failed.
      return Http::Http1::CallbackResult::Error;
    }

    ASSERT(!deferred_end_stream_headers_);
    auto request =
        std::make_unique<HttpRequestFrame>(std::move(active_request_->request_headers_), true);
    request->optionalBuffer().move(buffered_body_);

    if (request->optionalBuffer().length() > 0) {
      request->headerMap().removeTransferEncoding();
      request->headerMap().setContentLength(request->optionalBuffer().length());
    }
    onDecodingSuccess(std::move(request), {});
  } else if (deferred_end_stream_headers_) {
    deferred_end_stream_headers_ = false;
    auto request =
        std::make_unique<HttpRequestFrame>(std::move(active_request_->request_headers_), true);
    onDecodingSuccess(std::move(request), {});
  } else {
    dispatchBufferedBody(true);
  }

  parser_->pause();
  return Http::Http1::CallbackResult::Success;
}

EncodingResult Http1ServerCodec::encode(const StreamFrame& frame, EncodingContext&) {
  if (!active_request_.has_value()) {
    ENVOY_LOG(debug, "Generic proxy HTTP1 server codec: no request for coming response");
    return absl::InvalidArgumentError("no request for coming response");
  }

  const bool response_end_stream = frame.frameFlags().endStream();

  if (auto* headers = dynamic_cast<const HttpResponseFrame*>(&frame); headers != nullptr) {
    ENVOY_LOG(debug, "Generic proxy HTTP1 codec: encoding response headers (end_stream={}):\n{}",
              response_end_stream, *headers->response_);

    active_request_->response_chunk_encoding_ =
        Utility::isChunked(*headers->response_, response_end_stream);

    auto status = Utility::encodeResponseHeaders(encoding_buffer_, *headers->response_,
                                                 active_request_->response_chunk_encoding_);
    if (!status.ok()) {
      ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to encode response headers: {}",
                status.message());
      return status;
    }

    // Encode the optional buffer if it exists. This is used for local response or for the
    // request/responses in single frame mode.
    if (headers->optionalBuffer().length() > 0) {
      ASSERT(response_end_stream);
      Utility::encodeBody(encoding_buffer_, headers->optionalBuffer(),
                          active_request_->response_chunk_encoding_, response_end_stream);
    }

  } else if (auto* body = dynamic_cast<const HttpRawBodyFrame*>(&frame); body != nullptr) {
    ENVOY_LOG(debug, "Generic proxy HTTP1 codec: encoding response body (end_stream={} size={})",
              response_end_stream, body->buffer().length());
    Utility::encodeBody(encoding_buffer_, body->buffer(), active_request_->response_chunk_encoding_,
                        response_end_stream);
  }

  const uint64_t encoded_size = encoding_buffer_.length();

  // Send the encoded data to the connection and reset the buffer for the next frame.
  callbacks_->writeToConnection(encoding_buffer_);
  ASSERT(encoding_buffer_.length() == 0);

  if (response_end_stream) {
    if (!active_request_->request_complete_) {
      ENVOY_LOG(debug,
                "Generic proxy HTTP1 server codec: response complete before request complete");
      return absl::InvalidArgumentError("response complete before request complete");
    }

    active_request_.reset();
    return encoded_size;
  }

  return encoded_size;
}

EncodingResult Http1ClientCodec::encode(const StreamFrame& frame, EncodingContext&) {
  const bool request_end_stream = frame.frameFlags().endStream();

  if (auto* headers = dynamic_cast<const HttpRequestFrame*>(&frame); headers != nullptr) {
    ENVOY_LOG(debug, "encoding request headers (end_stream={}):\n{}", request_end_stream,
              *headers->request_);

    ASSERT(!expect_response_.has_value());
    expect_response_ = ExpectResponse{};
    expect_response_->request_chunk_encoding_ =
        Utility::isChunked(*headers->request_, request_end_stream);
    expect_response_->head_request_ =
        headers->request_->getMethodValue() == Http::Headers::get().MethodValues.Head;

    auto status = Utility::encodeRequestHeaders(encoding_buffer_, *headers->request_,
                                                expect_response_->request_chunk_encoding_);
    if (!status.ok()) {
      ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to encode request headers: {}",
                status.message());
      return status;
    }

    // Encode the optional buffer if it exists. This is used for local response or for the
    // request/responses in single frame mode.
    if (headers->optionalBuffer().length() > 0) {
      ASSERT(request_end_stream);
      Utility::encodeBody(encoding_buffer_, headers->optionalBuffer(),
                          expect_response_->request_chunk_encoding_, request_end_stream);
    }

  } else if (auto* body = dynamic_cast<const HttpRawBodyFrame*>(&frame); body != nullptr) {
    ENVOY_LOG(debug, "encoding request body (end_stream={} size={})", request_end_stream,
              body->buffer().length());
    Utility::encodeBody(encoding_buffer_, body->buffer(), expect_response_->request_chunk_encoding_,
                        request_end_stream);
  }

  if (request_end_stream) {
    expect_response_->request_complete_ = true;
  }

  const uint64_t encoded_size = encoding_buffer_.length();

  // Send the encoded data to the connection and reset the buffer for the next frame.
  callbacks_->writeToConnection(encoding_buffer_);
  ASSERT(encoding_buffer_.length() == 0);

  return encoded_size;
}

Http::Http1::CallbackResult Http1ClientCodec::onMessageBeginImpl() {
  if (!expect_response_.has_value()) {
    ENVOY_LOG(error, "Generic proxy HTTP1 codec: unexpected HTTP response from upstream");
    return Http::Http1::CallbackResult::Error;
  }
  expect_response_->response_headers_ = Http::ResponseHeaderMapImpl::create();
  return Http::Http1::CallbackResult::Success;
}

Http::Http1::CallbackResult Http1ClientCodec::onHeadersCompleteImpl() {
  if (!parser_->isHttp11()) {
    ENVOY_LOG(error,
              "Generic proxy HTTP1 codec: unsupported HTTP version, only HTTP/1.1 is supported.");
    return Http::Http1::CallbackResult::Error;
  }

  expect_response_->response_headers_->setStatus(
      std::to_string(static_cast<uint16_t>(parser_->statusCode())));

  // Validate response headers.
  const auto validate_headers_status =
      Utility::validateResponseHeaders(*expect_response_->response_headers_, parser_->statusCode());
  if (!validate_headers_status.ok()) {
    ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to validate response headers: {}",
              validate_headers_status.message());
    return Http::Http1::CallbackResult::Error;
  }

  const bool non_end_stream = Utility::hasBody(*parser_, true, expect_response_->head_request_);
  expect_response_->response_has_body_ = non_end_stream;
  ENVOY_LOG(debug, "decoding response headers complete (end_stream={}):\n{}", !non_end_stream,
            *expect_response_->response_headers_);

  if (single_frame_mode_) {
    // Do nothing until the onMessageComplete callback if we are in single frame mode.
    return Http::Http1::CallbackResult::Success;
  } else if (non_end_stream) {
    auto request =
        std::make_unique<HttpResponseFrame>(std::move(expect_response_->response_headers_), false);
    onDecodingSuccess(std::move(request), {});
  } else {
    deferred_end_stream_headers_ = true;
  }

  return Http::Http1::CallbackResult::Success;
}

Http::Http1::CallbackResult Http1ClientCodec::onMessageCompleteImpl() {
  const auto status_code = parser_->statusCode();
  if (status_code < Envoy::Http::Code::OK) {
    // There is no difference between single frame mode and normal mode for 1xx responses
    // because they are headers only responses.

    ASSERT(buffered_body_.length() == 0);

    expect_response_->response_headers_.reset();
    buffered_body_.drain(buffered_body_.length());

    // 100 Continue response. Ignore it.
    // 101 Switching Protocols response. Ignore it because we don't support upgrade for now.
    // 102 Processing response. Ignore it.
    // 103 Early Hints response. Ignore it.
    // 104 Upload Resumption Supported response. Ignore it.

    // Return success to continue parsing the actual response.
    return Http::Http1::CallbackResult::Success;
  }

  if (single_frame_mode_) {
    // Check if the buffered body is too large.
    if (bufferedBodyOverflow()) {
      // Pause the parser to avoid further parsing.
      parser_->pause();
      // Tell the caller that the decoding failed.
      return Http::Http1::CallbackResult::Error;
    }

    ASSERT(!deferred_end_stream_headers_);
    auto response =
        std::make_unique<HttpResponseFrame>(std::move(expect_response_->response_headers_), true);
    response->optionalBuffer().move(buffered_body_);

    if (response->optionalBuffer().length() > 0) {
      response->headerMap().removeTransferEncoding();
      response->headerMap().setContentLength(response->optionalBuffer().length());
    }

    onDecodingSuccess(std::move(response), {});
  } else if (deferred_end_stream_headers_) {
    deferred_end_stream_headers_ = false;
    auto response =
        std::make_unique<HttpResponseFrame>(std::move(expect_response_->response_headers_), true);
    onDecodingSuccess(std::move(response), {});
  } else {
    dispatchBufferedBody(true);
  }
  // If both request and response is complete, reset the state and pause the parser.
  if (expect_response_->request_complete_) {
    parser_->pause();
    expect_response_.reset();
    return Http::Http1::CallbackResult::Success;
  } else {
    ENVOY_LOG(debug, "Generic proxy HTTP1 client codec: response complete before request complete");
    return Http::Http1::CallbackResult::Error;
  }
}

CodecFactoryPtr
Http1CodecFactoryConfig::createCodecFactory(const Protobuf::Message& config,
                                            Envoy::Server::Configuration::ServerFactoryContext&) {
  const auto& typed_config = dynamic_cast<const ProtoConfig&>(config);

  return std::make_unique<Http1CodecFactory>(
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(typed_config, single_frame_mode, true),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(typed_config, max_buffer_size, DEFAULT_MAX_BUFFER_SIZE));
}

REGISTER_FACTORY(Http1CodecFactoryConfig, CodecFactoryConfig);

} // namespace Http1
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
