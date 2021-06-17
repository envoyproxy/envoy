#include "source/common/http/http1/parser_impl.h"

#include <llhttp.h>

#include <iostream>

#include "source/common/common/assert.h"
#include "source/common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class HttpParserImpl::Impl {
public:
  Impl(llhttp_type_t type) {
    llhttp_init(&parser_, type, &settings_);
    llhttp_set_lenient_chunked_length(&parser_, 1);
  }

  Impl(llhttp_type_t type, void* data) : Impl(type) {
    parser_.data = data;
    settings_ = {
        [](llhttp_t* parser) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          auto status = conn_impl->onMessageBegin();
          return conn_impl->setAndCheckCallbackStatus(std::move(status));
        },
        [](llhttp_t* parser, const char* at, size_t length) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          auto status = conn_impl->onUrl(at, length);
          return conn_impl->setAndCheckCallbackStatus(std::move(status));
        },
        nullptr, // on_status
        [](llhttp_t* parser, const char* at, size_t length) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          auto status = conn_impl->onHeaderField(at, length);
          return conn_impl->setAndCheckCallbackStatus(std::move(status));
        },
        [](llhttp_t* parser, const char* at, size_t length) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          auto status = conn_impl->onHeaderValue(at, length);
          return conn_impl->setAndCheckCallbackStatus(std::move(status));
        },
        [](llhttp_t* parser) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          auto statusor = conn_impl->onHeadersComplete();
          return conn_impl->setAndCheckCallbackStatusOr(std::move(statusor));
        },
        [](llhttp_t* parser, const char* at, size_t length) -> int {
          static_cast<ParserCallbacks*>(parser->data)->bufferBody(at, length);
          return 0;
        },
        [](llhttp_t* parser) -> int {
          auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
          auto status = conn_impl->onMessageComplete();
          return conn_impl->setAndCheckCallbackStatusOr(std::move(status));
        },
        [](llhttp_t* parser) -> int {
          // A 0-byte chunk header is used to signal the end of the chunked body.
          // When this function is called, http-parser holds the size of the chunk in
          // parser->content_length. See
          // https://github.com/nodejs/http-parser/blob/v2.9.3/http_parser.h#L336
          const bool is_final_chunk = (parser->content_length == 0);
          static_cast<ParserCallbacks*>(parser->data)->onChunkHeader(is_final_chunk);
          return 0;
        },
        nullptr, // on_chunk_complete
        nullptr, // on_url_complete
        nullptr, // on_status_complete
        nullptr, // on_header_field_complete
        nullptr  // on_header_value_complete
    };
  }

  RcVal execute(const char* slice, int len) {
    llhttp_errno_t error;
    if (slice == nullptr || len == 0) {
      error = llhttp_finish(&parser_);
    } else {
      error = llhttp_execute(&parser_, slice, len);
    }
    size_t nread = len;
    // Adjust number of bytes read in case of error.
    if (error != HPE_OK) {
      nread = llhttp_get_error_pos(&parser_) - slice;
      // Resume after upgrade.
      if (error == HPE_PAUSED_UPGRADE) {
        error = HPE_OK;
        llhttp_resume_after_upgrade(&parser_);
      }
    }
    return {nread, error};
  }

  void resume() { llhttp_resume(&parser_); }

  ParserStatus pause() {
    // llhttp can only pause by returning a paused status in user callbacks.
    return ParserStatus::Paused;
  }

  int getErrno() { return llhttp_get_errno(&parser_); }

  uint16_t statusCode() const { return parser_.status_code; }

  int httpMajor() const { return parser_.http_major; }

  int httpMinor() const { return parser_.http_minor; }

  absl::optional<uint64_t> contentLength() const {
    if (has_content_length_) {
      return parser_.content_length;
    }
    return absl::nullopt;
  }

  void setHasContentLength(bool val) { has_content_length_ = val; }

  bool isChunked() const { return parser_.flags & F_CHUNKED; }

  absl::string_view methodName() const {
    return llhttp_method_name(static_cast<llhttp_method>(parser_.method));
  }

  int hasTransferEncoding() const { return parser_.flags & F_TRANSFER_ENCODING; }

private:
  llhttp_t parser_;
  llhttp_settings_s settings_;
  bool has_content_length_{true};
};

HttpParserImpl::HttpParserImpl(MessageType type, ParserCallbacks* data) {
  llhttp_type_t parser_type;
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
HttpParserImpl::~HttpParserImpl() = default;

HttpParserImpl::RcVal HttpParserImpl::execute(const char* slice, int len) {
  return impl_->execute(slice, len);
}

void HttpParserImpl::resume() { impl_->resume(); }

ParserStatus HttpParserImpl::pause() { return impl_->pause(); }

bool HttpParserImpl::isOk() { return impl_->getErrno() == HPE_OK; }

bool HttpParserImpl::isPaused() { return impl_->getErrno() == HPE_PAUSED; }

uint16_t HttpParserImpl::statusCode() const { return impl_->statusCode(); }

int HttpParserImpl::httpMajor() const { return impl_->httpMajor(); }

int HttpParserImpl::httpMinor() const { return impl_->httpMinor(); }

absl::optional<uint64_t> HttpParserImpl::contentLength() const { return impl_->contentLength(); }

void HttpParserImpl::setHasContentLength(bool val) { return impl_->setHasContentLength(val); }

bool HttpParserImpl::isChunked() const { return impl_->isChunked(); }

absl::string_view HttpParserImpl::methodName() const { return impl_->methodName(); }

absl::string_view HttpParserImpl::errnoName(int rc) const {
  return llhttp_errno_name(static_cast<llhttp_errno>(rc));
}

int HttpParserImpl::hasTransferEncoding() const { return impl_->hasTransferEncoding(); }

int HttpParserImpl::statusToInt(const ParserStatus code) const {
  // See
  // https://github.com/nodejs/llhttp/blob/a620012f3fd1b64ace16d31c52cd57b97ee7174c/src/native/api.h#L29-L36
  switch (code) {
  case ParserStatus::Error:
    return HPE_USER;
  case ParserStatus::Success:
    return HPE_OK;
  case ParserStatus::NoBody:
    return 1;
  case ParserStatus::NoBodyData:
    return 2;
  case ParserStatus::Paused:
    return HPE_PAUSED;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
