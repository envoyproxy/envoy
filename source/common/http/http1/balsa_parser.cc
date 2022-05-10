#include "source/common/http/http1/balsa_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/http/headers.h"

#include "absl/strings/match.h"

using ::quiche::BalsaFrameEnums;
using ::quiche::BalsaHeaders;

namespace Envoy {
namespace Http {
namespace Http1 {

namespace {
constexpr absl::string_view kHttp11Suffix = "HTTP/1.1";
} // anonymous namespace

BalsaParser::BalsaParser(MessageType type, ParserCallbacks* connection, size_t max_header_length)
    : connection_(connection) {
  ASSERT(connection_ != nullptr);

  framer_.set_balsa_headers(&headers_);
  framer_.set_balsa_visitor(this);
  framer_.set_max_header_length(max_header_length);

  switch (type) {
  case MessageType::Request:
    framer_.set_is_request(true);
    break;
  case MessageType::Response:
    framer_.set_is_request(false);
    framer_.set_balsa_trailer(&trailers_);
    break;
  }
}

size_t BalsaParser::execute(const char* slice, int len) {
  ASSERT(status_ != ParserStatus::Error);
  // ASSERT(false, std::string(slice, len)); // TODO remove
  return framer_.ProcessInput(slice, len);
}

void BalsaParser::resume() {
  ASSERT(status_ != ParserStatus::Error);
  status_ = ParserStatus::Ok;
}

CallbackResult BalsaParser::pause() {
  ASSERT(status_ != ParserStatus::Error);
  status_ = ParserStatus::Paused;
  return CallbackResult::Success;
}

ParserStatus BalsaParser::getStatus() { return status_; }

uint16_t BalsaParser::statusCode() const { return headers_.parsed_response_code(); }

bool BalsaParser::isHttp11() const { return absl::EndsWith(headers_.first_line(), kHttp11Suffix); }

absl::optional<uint64_t> BalsaParser::contentLength() const {
  if (!headers_.content_length_valid()) {
    return absl::nullopt;
  }
  return headers_.content_length();
}

bool BalsaParser::isChunked() const { return headers_.transfer_encoding_is_chunked(); }

absl::string_view BalsaParser::methodName() const { return headers_.request_method(); }

absl::string_view BalsaParser::errorMessage() const { return error_message_; }

int BalsaParser::hasTransferEncoding() const { return headers_.HasHeader("transfer-encoding"); }

void BalsaParser::OnRawBodyInput(absl::string_view /*input*/) {}

void BalsaParser::OnBodyChunkInput(absl::string_view input) {
  if (status_ == ParserStatus::Error) {
    return;
  }

  connection_->bufferBody(input.data(), input.size());
}

void BalsaParser::OnHeaderInput(absl::string_view /*input*/) {}
void BalsaParser::OnTrailerInput(absl::string_view /*input*/) {}

void BalsaParser::ProcessHeaders(const BalsaHeaders& headers) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  headers.ForEachHeader([this](const absl::string_view key, const absl::string_view value) {
    checkResult(connection_->onHeaderField(key.data(), key.length()));
    if (status_ == ParserStatus::Error) {
      return false;
    }
    checkResult(connection_->onHeaderValue(value.data(), value.length()));
    if (status_ == ParserStatus::Error) {
      return false;
    }
    return true;
  });
}

void BalsaParser::ProcessTrailers(const BalsaHeaders& trailer) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  trailer.ForEachHeader([this](const absl::string_view key, const absl::string_view value) {
    checkResult(connection_->onHeaderField(key.data(), key.length()));
    if (status_ == ParserStatus::Error) {
      return false;
    }
    checkResult(connection_->onHeaderValue(value.data(), value.length()));
    if (status_ == ParserStatus::Error) {
      return false;
    }
    return true;
  });
}

void BalsaParser::OnRequestFirstLineInput(absl::string_view /*line_input*/,
                                          absl::string_view method_input,
                                          absl::string_view request_uri,
                                          absl::string_view /*version_input*/) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  checkResult(connection_->onMessageBegin());
  if (status_ == ParserStatus::Error) {
    return;
  }
  const bool is_connect = method_input == Headers::get().MethodValues.Connect;
  if (!isUrlValid(request_uri, is_connect)) {
    status_ = ParserStatus::Error;
    return;
  }
  checkResult(connection_->onUrl(request_uri.data(), request_uri.size()));
}

void BalsaParser::OnResponseFirstLineInput(absl::string_view /*line_input*/,
                                           absl::string_view /*version_input*/,
                                           absl::string_view status_input,
                                           absl::string_view /*reason_input*/) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  checkResult(connection_->onMessageBegin());
  if (status_ == ParserStatus::Error) {
    return;
  }
  checkResult(connection_->onStatus(status_input.data(), status_input.size()));
}

void BalsaParser::OnChunkLength(size_t chunk_length) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  const bool is_final_chunk = chunk_length == 0;
  connection_->onChunkHeader(is_final_chunk);
}

void BalsaParser::OnChunkExtensionInput(absl::string_view /*input*/) {}

void BalsaParser::HeaderDone() {
  if (status_ == ParserStatus::Error) {
    return;
  }
  checkResult(connection_->onHeadersComplete());
}

void BalsaParser::ContinueHeaderDone() {}

void BalsaParser::MessageDone() {
  if (status_ == ParserStatus::Error) {
    return;
  }
  checkResult(connection_->onMessageComplete());
  framer_.Reset();
}

void BalsaParser::HandleError(BalsaFrameEnums::ErrorCode error_code) {
  status_ = ParserStatus::Error;
  error_message_ = BalsaFrameEnums::ErrorCodeToString(error_code);
  // ASSERT(false, std::string(error_message_));
  if (error_code == BalsaFrameEnums::UNKNOWN_TRANSFER_ENCODING) {
    error_message_ = "unsupported transfer encoding";
  }
  if (error_code == BalsaFrameEnums::INVALID_CHUNK_LENGTH) {
    error_message_ = "HPE_INVALID_CHUNK_SIZE";
  }
  if (error_code == BalsaFrameEnums::HEADERS_TOO_LONG) {
    // TODO "headers size", "trailers size"
    // TODO `error_message_` will need to be std::string instead of
    // absl::string_view if it holds string constructed by StrCar
    // TODO this error is prefixed by "http/1.1 protocol error: " in
    // ConnectionImpl::dispatchSlice(), but sometimes it should not be.
    error_message_ = "size exceeds limit";
  }
}

void BalsaParser::HandleWarning(BalsaFrameEnums::ErrorCode /*error_code*/) {}

void BalsaParser::checkResult(CallbackResult result) {
  if (result == CallbackResult::Error) {
    status_ = ParserStatus::Error;
  }
}

// This method is crafted to match the URL validation behavior of the http-parser library.
bool BalsaParser::isUrlValid(absl::string_view url, bool is_connect) {
  if (url.empty()) {
    return false;
  }

  // Path (and query) starts with the first '/' or '?' character.
  const auto is_path_query_start = [](char c) { return c == '/' || c == '?'; };

  const auto is_valid_path_query_char = [](char c) {
    return c == 9 || c == 12 || ('!' <= c && c <= 126);
  };

  // The URL starts with path or query.
  if (auto it = url.begin(); is_path_query_start(*it)) {
    ++it;
    return std::all_of(it, url.end(), is_valid_path_query_char);
  }

  if (!is_connect) {
    // Scheme must be alpha and non-empty.
    auto it = std::find_if_not(url.begin(), url.end(), [](char c) { return std::isalpha(c); });
    if (it == url.begin()) {
      return false;
    }
    url.remove_prefix(it - url.begin());

    constexpr absl::string_view kColonSlashSlash = "://";
    if (!absl::StartsWith(url, kColonSlashSlash)) {
      return false;
    }
    url.remove_prefix(kColonSlashSlash.length());
  }

  // Divide the rest of the URL into two sections: host, and path/query/fragments.
  auto path_query_begin = std::find_if(url.begin(), url.end(), is_path_query_start);

  const absl::string_view host = url.substr(0, path_query_begin - url.begin());
  const absl::string_view path_query = url.substr(path_query_begin - url.begin());

  const auto valid_host_char = [](char c) {
    return std::isalnum(c) || c == '!' || c == '$' || c == '%' || c == '&' || c == '\'' ||
           c == '(' || c == ')' || c == '*' || c == '+' || c == ',' || c == '-' || c == '.' ||
           c == ':' || c == ';' || c == '=' || c == '@' || c == '[' || c == ']' || c == '_' ||
           c == '~';
  };

  if (!std::all_of(host.begin(), host.end(), valid_host_char)) {
    return false;
  }

  if (absl::StrContains(host, "@@")) {
    return false;
  }

  return std::all_of(path_query.begin(), path_query.end(), is_valid_path_query_char);
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
