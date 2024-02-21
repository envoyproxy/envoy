#include "contrib/generic_proxy/filters/network/source/codecs/http1/config.h"

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
      buffer.add("Transfer-Encoding: chunked\r\n");
    }
  }
}

bool Utility::isChunked(const Http::RequestOrResponseHeaderMap& headers, bool bodiless_message) {
  // If the message has body and the content length is not set, then treat it as chunked.
  // Note all upgrade and connect requests are rejected so this is safe.
  return !bodiless_message && headers.ContentLength() == nullptr;
}

absl::Status Utility::validateHeaders(const Http::RequestHeaderMap& headers) {
  // No upgrade and connect support for now.
  // TODO(wbpcode): add support for upgrade and connect in the future.
  if (Http::Utility::isUpgrade(headers) ||
      headers.getMethodValue() == Http::Headers::get().MethodValues.Connect) {
    return absl::InvalidArgumentError("upgrade or connect are not supported");
  }

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

  // One of method, path, host is missing.
  if (headers.Method() == nullptr || headers.Path() == nullptr || headers.Host() == nullptr) {
    return absl::InvalidArgumentError("missing required headers");
  }

  return absl::OkStatus();
}

absl::Status Utility::validateHeaders(const Http::ResponseHeaderMap& headers) {

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

  // Status is missing.
  if (headers.Status() == nullptr) {
    return absl::InvalidArgumentError("missing required headers");
  }

  return absl::OkStatus();
}

absl::Status Utility::encodeHeaders(Buffer::Instance& buffer, const Http::RequestHeaderMap& headers,
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

absl::Status Utility::encodeHeaders(Buffer::Instance& buffer,
                                    const Http::ResponseHeaderMap& headers, bool chunk_encoding) {
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

void Utility::encodeBody(Buffer::Instance& buffer, const HttpRawBodyFrame& body,
                         bool chunk_encoding, bool end_stream) {
  if (body.length() > 0) {
    // Chunk header.
    if (chunk_encoding) {
      buffer.add(absl::StrCat(absl::Hex(body.length()), CRLF));
    }

    // Body.
    body.moveTo(buffer);

    // Chunk footer.
    if (chunk_encoding) {
      buffer.add(CRLF);
    }
  }

  // Add additional LAST_CHUNK if this is the last frame and chunk encoding is enabled.
  if (end_stream) {
    if (chunk_encoding) {
      buffer.addFragments({LAST_CHUNK, CRLF});
    }
  }
}

void Http1ServerCodec::encode(const StreamFrame& frame, EncodingCallbacks& callbacks) {
  const bool end_stream = frame.frameFlags().endStream();

  if (auto* headers = dynamic_cast<const HttpResponseFrame*>(&frame); headers != nullptr) {
    ENVOY_LOG(debug, "encoding response headers (end_stream={}):\n{}", end_stream,
              *headers->response_);

    chunk_encoding_ = Utility::isChunked(*headers->response_, end_stream);

    auto status = Utility::encodeHeaders(response_buffer_, *headers->response_, chunk_encoding_);
    if (!status.ok()) {
      ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to encode response headers: {}",
                status.message());
      callbacks_->connection()->close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  } else if (auto* body = dynamic_cast<const HttpRawBodyFrame*>(&frame); body != nullptr) {
    ENVOY_LOG(debug, "encoding response body (end_stream={} size={})", end_stream, body->length());
    Utility::encodeBody(response_buffer_, *body, chunk_encoding_, end_stream);
  }

  callbacks.onEncodingSuccess(response_buffer_, end_stream);
  if (end_stream) {
    // Reset the chunk encoding flag after the last frame.
    chunk_encoding_ = false;
    active_request_ = false;
  }
}

void Http1ClientCodec::encode(const StreamFrame& frame, EncodingCallbacks& callbacks) {
  const bool end_stream = frame.frameFlags().endStream();

  if (auto* headers = dynamic_cast<const HttpRequestFrame*>(&frame); headers != nullptr) {
    ENVOY_LOG(debug, "encoding request headers (end_stream={}):\n{}", end_stream,
              *headers->request_);
    chunk_encoding_ = Utility::isChunked(*headers->request_, end_stream);

    auto status = Utility::encodeHeaders(request_buffer_, *headers->request_, chunk_encoding_);
    if (!status.ok()) {
      ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to encode request headers: {}",
                status.message());
      callbacks_->connection()->close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  } else if (auto* body = dynamic_cast<const HttpRawBodyFrame*>(&frame); body != nullptr) {
    ENVOY_LOG(debug, "encoding request body (end_stream={} size={})", end_stream, body->length());
    Utility::encodeBody(request_buffer_, *body, chunk_encoding_, end_stream);
  }

  callbacks.onEncodingSuccess(request_buffer_, end_stream);
  if (end_stream) {
    // Reset the chunk encoding flag after the last frame.
    chunk_encoding_ = false;
  }
}

CodecFactoryPtr
HttpCodecFactoryConfig::createCodecFactory(const Protobuf::Message&,
                                           Envoy::Server::Configuration::FactoryContext&) {
  return std::make_unique<HttpCodecFactory>();
}

REGISTER_FACTORY(HttpCodecFactoryConfig, CodecFactoryConfig);

} // namespace Http1
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
