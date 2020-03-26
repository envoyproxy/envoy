#include "common/http/status.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

namespace {

std::string StatusCodeToString(StatusCode code) {
  switch (code) {
  default:
    return "";
  case StatusCode::Ok:
    return "OK";
  case StatusCode::CodecProtocolError:
    return "CodecProtocolError";
  case StatusCode::BufferFloodError:
    return "BufferFloodError";
  case StatusCode::PrematureResponse:
    return "PrematureResponse";
  case StatusCode::CodecClientError:
    return "CodecClientError";
  }
}

} // namespace

void Status::Unref() {
  // Fast path: if ref==1, there is no need for a RefCountDec (since
  // this is the only reference and therefore no other thread is
  // allowed to be mucking with r).
  if (rep_ && (rep_->ref_.load(std::memory_order_acquire) == 1 ||
               rep_->ref_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0)) {
    delete rep_;
  }
}

Status::Status(StatusCode code, absl::string_view msg)
    : rep_(code != StatusCode::Ok ? new StatusRep(code, msg) : nullptr) {
  Ref();
}

Status::Status(StatusCode code, Http::Code http_code, absl::string_view msg)
    : rep_(code != StatusCode::Ok ? new StatusRep(code, http_code, msg) : nullptr) {
  Ref();
}

Status::~Status() { Unref(); }

const std::string* Status::EmptyString() {
  static std::string* empty_string = new std::string;
  return empty_string;
}

std::string Status::ToStringError() const {
  std::string text;
  if (rep_->http_code_.has_value()) {
    absl::StrAppend(&text, StatusCodeToString(Code()), ", HTTP code ", rep_->http_code_.value(),
                    ": ", Message());
  } else {
    absl::StrAppend(&text, StatusCodeToString(Code()), ": ", Message());
  }
  return text;
}

} // namespace Http
} // namespace Envoy
