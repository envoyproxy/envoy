#include "common/http/http1/llhttp_parser.h"

#include <llhttp.h>

#include "common/common/assert.h"
#include "common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class LlHttpParserImpl::Impl {
public:
  Impl(llhttp_type_t type, void* data) {
    llhttp_init(&parser_, type, &settings_);
    parser_.data = data;
  }

  size_t execute(const char* slice, int len) {
    llhttp_errno_t err;
    if (slice == nullptr || len == 0) {
      err = llhttp_finish(&parser_);
    } else {
      err = llhttp_execute(&parser_, slice, len);
    }

    size_t nread = len;
    if (err != HPE_OK) {
      nread = llhttp_get_error_pos(&parser_) - slice;
      if (err == HPE_PAUSED_UPGRADE) {
        err = HPE_OK;
        llhttp_resume_after_upgrade(&parser_);
      }
    }

    return nread;
  }

  void resume() { llhttp_resume(&parser_); }

  int getErrno() { return llhttp_get_errno(&parser_); }

  int statusCode() const { return parser_.status_code; }

  int httpMajor() const { return parser_.http_major; }

  int httpMinor() const { return parser_.http_minor; }

  uint64_t contentLength() const { return parser_.content_length; }

  int flags() const { return parser_.flags; }

  uint16_t method() const { return parser_.method; }

  const char* methodName() const {
    return llhttp_method_name(static_cast<llhttp_method_t>(parser_.method));
  }

private:
  llhttp_t parser_;
  llhttp_settings_s settings_;
};

LlHttpParserImpl::LlHttpParserImpl(MessageType type, void* data) {
  llhttp_type_t llhttp_type;
  switch (type) {
  case MessageType::Request:
    llhttp_type = HTTP_REQUEST;
    break;
  case MessageType::Response:
    llhttp_type = HTTP_RESPONSE;
    break;
  default:
    // We strictly use the parser for either request or response, not both.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  impl_ = std::make_unique<Impl>(llhttp_type, data);
}

LlHttpParserImpl::~LlHttpParserImpl() = default;

int LlHttpParserImpl::execute(const char* slice, int len) { return impl_->execute(slice, len); }

void LlHttpParserImpl::resume() { impl_->resume(); }

int LlHttpParserImpl::pause() {
  // TODO(dereka) do we actually need to call llhttp_pause(&parser_); ?
  return HPE_PAUSED;
}

int LlHttpParserImpl::getErrno() { return impl_->getErrno(); }

int LlHttpParserImpl::statusCode() const { return impl_->statusCode(); }

int LlHttpParserImpl::httpMajor() const { return impl_->httpMajor(); }

int LlHttpParserImpl::httpMinor() const { return impl_->httpMinor(); }

uint64_t LlHttpParserImpl::contentLength() const { return impl_->contentLength(); }

int LlHttpParserImpl::flags() const { return impl_->flags(); }

uint16_t LlHttpParserImpl::method() const { return impl_->method(); }

const char* LlHttpParserImpl::methodName() const { return impl_->methodName(); }

const char* LlHttpParserImpl::errnoName() {
  return llhttp_errno_name(static_cast<llhttp_errno_t>(impl_->getErrno()));
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
