#include "common/http/http1/legacy_http_parser.h"

#include <iostream>

#include <http_parser.h>

#include "common/common/assert.h"
#include "common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class LegacyHttpParserImpl::Impl {
public:
  // Possible idea: have an overload that doesn't accept `data` which appears
  // to just be used for callbacks? If no data, then leave settings as nullptrs?
  // https://github.com/nodejs/http-parser/blob/master/http_parser.h#L320

  // so far unused
  Impl(http_parser_type type) {
    http_parser_init(&parser_, type);
  }

  Impl(http_parser_type type, void* data) : Impl(type) {
    parser_.data = data;
    settings_ = {
      [](http_parser* parser) -> int {
        std::cout << "message begin callback" << std::endl;
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
        return static_cast<ParserCallbacks*>(parser->data)->onBody(at, length);
      },
      [](http_parser* parser) -> int {
        return static_cast<ParserCallbacks*>(parser->data)->onMessageComplete();
      },
      nullptr, // TODO(dereka) onChunkHeader
      nullptr // TODO(dereka) onChunkComplete
    };
  }

  size_t execute(const char* slice, int len) {
    return http_parser_execute(&parser_, &settings_, slice, len);
  }

  void resume() {
    http_parser_pause(&parser_, 0);
  }

  int pause() {
    http_parser_pause(&parser_, 1);
    return HPE_PAUSED;
  }

  int getErrno() {
    return HTTP_PARSER_ERRNO(&parser_);
  }

  int statusCode() const {
    return parser_.status_code;
  }

  int httpMajor() const {
    return parser_.http_major;
  }

  int httpMinor() const {
    return parser_.http_minor;
  }

  uint64_t contentLength() const {
    return parser_.content_length;
  }

  int flags() const {
    return parser_.flags;
  }

  uint16_t method() const {
    return parser_.method;
  }

  const char* methodName() const {
    return http_method_str(static_cast<http_method>(parser_.method));
  }

private:
  http_parser parser_;
  http_parser_settings settings_;
};

LegacyHttpParserImpl::LegacyHttpParserImpl(MessageType type, void* data) {
  http_parser_type parser_type;
  switch (type) {
  case MessageType::Request:
    parser_type = HTTP_REQUEST;
    break;
  case MessageType::Response:
    parser_type = HTTP_RESPONSE;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  impl_ = std::make_unique<Impl>(parser_type, data);
}

// Because we have a pointer-to-impl using std::unique_ptr, we must place the destructor in the
// same compilation unit so that the destructor has a complete definition of Impl.
LegacyHttpParserImpl::~LegacyHttpParserImpl() = default;

int LegacyHttpParserImpl::execute(const char* slice, int len) {
  return impl_->execute(slice, len);
}

void LegacyHttpParserImpl::resume() {
  impl_->resume();
}

int LegacyHttpParserImpl::pause() {
  return impl_->pause();
}

int LegacyHttpParserImpl::getErrno() {
  return impl_->getErrno();
}

int LegacyHttpParserImpl::statusCode() const {
  return impl_->statusCode();
}

int LegacyHttpParserImpl::httpMajor() const {
  return impl_->httpMajor();
}

int LegacyHttpParserImpl::httpMinor() const {
  return impl_->httpMinor();
}

uint64_t LegacyHttpParserImpl::contentLength() const {
  return impl_->contentLength();
}

int LegacyHttpParserImpl::flags() const {
  return impl_->flags();
}

uint16_t LegacyHttpParserImpl::method() const {
  return impl_->method();
}

const char* LegacyHttpParserImpl::methodName() const {
  return impl_->methodName();
}

const char* LegacyHttpParserImpl::errnoName() {
  return http_errno_name(static_cast<http_errno>(impl_->getErrno()));
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
