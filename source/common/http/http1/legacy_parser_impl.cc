#include "common/http/http1/legacy_parser_impl.h"

#include <http_parser.h>

#include <iostream>

#include "common/common/assert.h"
#include "common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

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
          return static_cast<ParserCallbacks*>(parser->data)->onMessageBegin();
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          return static_cast<ParserCallbacks*>(parser->data)->onUrl(at, length);
        },
        // TODO(dereka) onStatus
        nullptr,
        [](http_parser* parser, const char* at, size_t length) -> int {
          return static_cast<ParserCallbacks*>(parser->data)->onHeaderField(at, length);
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          return static_cast<ParserCallbacks*>(parser->data)->onHeaderValue(at, length);
        },
        [](http_parser* parser) -> int {
          return static_cast<ParserCallbacks*>(parser->data)->onHeadersComplete();
        },
        [](http_parser* parser, const char* at, size_t length) -> int {
          return static_cast<ParserCallbacks*>(parser->data)->bufferBody(at, length);
        },
        [](http_parser* parser) -> int {
          return static_cast<ParserCallbacks*>(parser->data)->onMessageComplete();
        },
        [](http_parser* parser) -> int {
          // A 0-byte chunk header is used to signal the end of the chunked body.
          // When this function is called, http-parser holds the size of the chunk in
          // parser->content_length. See
          // https://github.com/nodejs/http-parser/blob/v2.9.3/http_parser.h#L336
          const bool is_final_chunk = (parser->content_length == 0);
          return static_cast<ParserCallbacks*>(parser->data)->onChunkHeader(is_final_chunk);
        },
        nullptr // TODO(dereka) onChunkComplete
    };
  }

  RcVal execute(const char* slice, int len) {
    return {http_parser_execute(&parser_, &settings_, slice, len), HTTP_PARSER_ERRNO(&parser_)};
  }

  void resume() { http_parser_pause(&parser_, 0); }

  ParserStatus pause() {
    http_parser_pause(&parser_, 1);
    // http_parser pauses through http_parser_pause above.
    return ParserStatus::Success;
  }

  int getErrno() { return HTTP_PARSER_ERRNO(&parser_); }

  int statusCode() const { return parser_.status_code; }

  int httpMajor() const { return parser_.http_major; }

  int httpMinor() const { return parser_.http_minor; }

  uint64_t contentLength() const { return parser_.content_length; }

  int flags() const { return parser_.flags; }

  uint16_t method() const { return parser_.method; }

  const char* methodName() const {
    return http_method_str(static_cast<http_method>(parser_.method));
  }

  int usesTransferEncoding() const { return parser_.uses_transfer_encoding; }

  bool seenContentLength() const {
    // An unset content length will be have all bits set.
    // See
    // https://github.com/nodejs/http-parser/blob/ec8b5ee63f0e51191ea43bb0c6eac7bfbff3141d/http_parser.h#L311
    return (parser_.content_length & (parser_.content_length & 1)) == 0;
  }

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
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  impl_ = std::make_unique<Impl>(parser_type, data);
}

// Because we have a pointer-to-impl using std::unique_ptr, we must place the destructor in the
// same compilation unit so that the destructor has a complete definition of Impl.
LegacyHttpParserImpl::~LegacyHttpParserImpl() = default;

LegacyHttpParserImpl::RcVal LegacyHttpParserImpl::execute(const char* slice, int len) {
  return impl_->execute(slice, len);
}

void LegacyHttpParserImpl::resume() { impl_->resume(); }

ParserStatus LegacyHttpParserImpl::pause() { return impl_->pause(); }

int LegacyHttpParserImpl::getErrno() { return impl_->getErrno(); }

int LegacyHttpParserImpl::statusCode() const { return impl_->statusCode(); }

int LegacyHttpParserImpl::httpMajor() const { return impl_->httpMajor(); }

int LegacyHttpParserImpl::httpMinor() const { return impl_->httpMinor(); }

uint64_t LegacyHttpParserImpl::contentLength() const { return impl_->contentLength(); }

int LegacyHttpParserImpl::flags() const { return impl_->flags(); }

uint16_t LegacyHttpParserImpl::method() const { return impl_->method(); }

const char* LegacyHttpParserImpl::methodName() const { return impl_->methodName(); }

const char* LegacyHttpParserImpl::errnoName() {
  return http_errno_name(static_cast<http_errno>(impl_->getErrno()));
}

const char* LegacyHttpParserImpl::errnoName(int rc) const {
  return http_errno_name(static_cast<http_errno>(rc));
}

int LegacyHttpParserImpl::usesTransferEncoding() const { return impl_->usesTransferEncoding(); }

bool LegacyHttpParserImpl::seenContentLength() const { return impl_->seenContentLength(); }

int LegacyHttpParserImpl::statusToInt(const ParserStatus code) const {
  // See
  // https://github.com/nodejs/http-parser/blob/5c5b3ac62662736de9e71640a8dc16da45b32503/http_parser.h#L72.
  switch (code) {
  case ParserStatus::Error:
    return -1;
  case ParserStatus::Success:
    return 0;
  case ParserStatus::NoBody:
    return 1;
  case ParserStatus::NoBodyData:
    return 2;
  case ParserStatus::Paused:
    return 31;
  }
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
