#include "source/common/http/http1/legacy_parser_impl.h"

#include <http_parser.h>

#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

ParserStatus intToStatus(int rc) {
  switch (rc) {
  case 0:
    return ParserStatus::Ok;
  case 31:
    return ParserStatus::Paused;
  default:
    return ParserStatus::Error;
  }
}

} // namespace

class LegacyHttpParserImpl::Impl {
public:
  Impl(http_parser_type type) {
    http_parser_init(&parser_, type);
    parser_.allow_chunked_length = 1;
  }

  Impl(http_parser_type type, void* data) : Impl(type) {
    parser_.data = data;
    settings_ = {
        [](http_parser* parser) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          return static_cast<int>(conn_impl->onMessageBegin());
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          return static_cast<int>(conn_impl->onUrl(at, length));
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          return static_cast<int>(conn_impl->onStatus(at, length));
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          return static_cast<int>(conn_impl->onHeaderField(at, length));
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          return static_cast<int>(conn_impl->onHeaderValue(at, length));
        },
        [](http_parser* parser) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          return static_cast<int>(conn_impl->onHeadersComplete());
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          static_cast<ParserCallbacks*>(parser->data)->bufferBody(at, length);
          return 0;
        },
        [](http_parser* parser) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          return static_cast<int>(conn_impl->onMessageComplete());
        },
        [](http_parser* parser) -> int {
          // A 0-byte chunk header is used to signal the end of the chunked body.
          // When this function is called, http-parser holds the size of the chunk in
          // parser->content_length. See
          // https://github.com/nodejs/http-parser/blob/v2.9.3/http_parser.h#L336
          const bool is_final_chunk = (parser->content_length == 0);
          static_cast<ParserCallbacks*>(parser->data)->onChunkHeader(is_final_chunk);
          return 0;
        },
        nullptr // on_chunk_complete
    };
  }

  size_t execute(const char* slice, int len) {
    return http_parser_execute(&parser_, &settings_, slice, len);
  }

  void resume() { http_parser_pause(&parser_, 0); }

  CallbackResult pause() {
    http_parser_pause(&parser_, 1);
    return CallbackResult::Success;
  }

  int getErrno() { return HTTP_PARSER_ERRNO(&parser_); }

  uint16_t statusCode() const { return parser_.status_code; }

  bool isHttp11() const { return parser_.http_major == 1 && parser_.http_minor == 1; }

  absl::optional<uint64_t> contentLength() const {
    // An unset content length will be have all bits set.
    // See
    // https://github.com/nodejs/http-parser/blob/ec8b5ee63f0e51191ea43bb0c6eac7bfbff3141d/http_parser.h#L311
    if (parser_.content_length == ULLONG_MAX) {
      return absl::nullopt;
    }
    return parser_.content_length;
  }

  bool isChunked() const { return parser_.flags & F_CHUNKED; }

  absl::string_view methodName() const {
    return http_method_str(static_cast<http_method>(parser_.method));
  }

  int hasTransferEncoding() const { return parser_.uses_transfer_encoding; }

private:
  http_parser parser_;
  http_parser_settings settings_;
};

LegacyHttpParserImpl::LegacyHttpParserImpl(MessageType type, ParserCallbacks* data) {
  http_parser_type parser_type;
  switch (type) {
  case MessageType::Request:
    parser_type = HTTP_REQUEST;
    break;
  case MessageType::Response:
    parser_type = HTTP_RESPONSE;
    break;
  }

  impl_ = std::make_unique<Impl>(parser_type, data);
}

// Because we have a pointer-to-impl using std::unique_ptr, we must place the destructor in the
// same compilation unit so that the destructor has a complete definition of Impl.
LegacyHttpParserImpl::~LegacyHttpParserImpl() = default;

size_t LegacyHttpParserImpl::execute(const char* slice, int len) {
  return impl_->execute(slice, len);
}

void LegacyHttpParserImpl::resume() { impl_->resume(); }

CallbackResult LegacyHttpParserImpl::pause() { return impl_->pause(); }

ParserStatus LegacyHttpParserImpl::getStatus() const { return intToStatus(impl_->getErrno()); }

uint16_t LegacyHttpParserImpl::statusCode() const { return impl_->statusCode(); }

bool LegacyHttpParserImpl::isHttp11() const { return impl_->isHttp11(); }

absl::optional<uint64_t> LegacyHttpParserImpl::contentLength() const {
  return impl_->contentLength();
}

bool LegacyHttpParserImpl::isChunked() const { return impl_->isChunked(); }

absl::string_view LegacyHttpParserImpl::methodName() const { return impl_->methodName(); }

absl::string_view LegacyHttpParserImpl::errorMessage() const {
  return http_errno_name(static_cast<http_errno>(impl_->getErrno()));
}

int LegacyHttpParserImpl::hasTransferEncoding() const { return impl_->hasTransferEncoding(); }

} // namespace Http1
} // namespace Http
} // namespace Envoy
