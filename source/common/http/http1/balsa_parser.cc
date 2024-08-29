#include "source/common/http/http1/balsa_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Http {
namespace Http1 {

namespace {

using ::quiche::BalsaFrameEnums;
using ::quiche::BalsaHeaders;

constexpr absl::string_view kColonSlashSlash = "://";
// Response must start with "HTTP".
constexpr char kResponseFirstByte = 'H';
constexpr absl::string_view kHttpVersionPrefix = "HTTP/";

// Allowed characters for field names according to Section 5.1
// and for methods according to Section 9.1 of RFC 9110:
// https://www.rfc-editor.org/rfc/rfc9110.html
constexpr absl::string_view kValidCharacters =
    "!#$%&'*+-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ^_`abcdefghijklmnopqrstuvwxyz|~";
constexpr absl::string_view::iterator kValidCharactersBegin = kValidCharacters.begin();
constexpr absl::string_view::iterator kValidCharactersEnd = kValidCharacters.end();

bool isFirstCharacterOfValidMethod(char c) {
  static constexpr char kValidFirstCharacters[] = {'A', 'B', 'C', 'D', 'G', 'H', 'L', 'M',
                                                   'N', 'O', 'P', 'R', 'S', 'T', 'U'};

  const auto* begin = &kValidFirstCharacters[0];
  const auto* end = &kValidFirstCharacters[ABSL_ARRAYSIZE(kValidFirstCharacters) - 1] + 1;
  return std::binary_search(begin, end, c);
}

// TODO(#21245): Skip method validation altogether when UHV method validation is
// enabled.
bool isMethodValid(absl::string_view method, bool allow_custom_methods) {
  if (allow_custom_methods) {
    return !method.empty() &&
           std::all_of(method.begin(), method.end(), [](absl::string_view::value_type c) {
             return std::binary_search(kValidCharactersBegin, kValidCharactersEnd, c);
           });
  }

  static constexpr absl::string_view kValidMethods[] = {
      "ACL",       "BIND",    "CHECKOUT", "CONNECT", "COPY",       "DELETE",     "GET",
      "HEAD",      "LINK",    "LOCK",     "MERGE",   "MKACTIVITY", "MKCALENDAR", "MKCOL",
      "MOVE",      "MSEARCH", "NOTIFY",   "OPTIONS", "PATCH",      "POST",       "PROPFIND",
      "PROPPATCH", "PURGE",   "PUT",      "REBIND",  "REPORT",     "SEARCH",     "SOURCE",
      "SUBSCRIBE", "TRACE",   "UNBIND",   "UNLINK",  "UNLOCK",     "UNSUBSCRIBE"};

  const auto* begin = &kValidMethods[0];
  const auto* end = &kValidMethods[ABSL_ARRAYSIZE(kValidMethods) - 1] + 1;
  return std::binary_search(begin, end, method);
}

// This function is crafted to match the URL validation behavior of the http-parser library.
bool isUrlValid(absl::string_view url, bool is_connect) {
  if (url.empty()) {
    return false;
  }

  // Same set of characters are allowed for path and query.
  const auto is_valid_path_query_char = [](char c) {
    return c == 9 || c == 12 || ('!' <= c && c <= 126);
  };

  // The URL may start with a path.
  if (auto it = url.begin(); *it == '/' || *it == '*') {
    ++it;
    return std::all_of(it, url.end(), is_valid_path_query_char);
  }

  // If method is not CONNECT, parse scheme.
  if (!is_connect) {
    // Scheme must start with alpha and be non-empty.
    auto it = url.begin();
    if (!std::isalpha(*it)) {
      return false;
    }
    ++it;
    // Scheme started with an alpha character and the rest of it is alpha, digit, '+', '-' or '.'.
    const auto is_scheme_suffix = [](char c) {
      return std::isalpha(c) || std::isdigit(c) || c == '+' || c == '-' || c == '.';
    };
    it = std::find_if_not(it, url.end(), is_scheme_suffix);
    url.remove_prefix(it - url.begin());
    if (!absl::StartsWith(url, kColonSlashSlash)) {
      return false;
    }
    url.remove_prefix(kColonSlashSlash.length());
  }

  // Path and query start with the first '/' or '?' character.
  const auto is_path_query_start = [](char c) { return c == '/' || c == '?'; };

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

  // Match http-parser's quirk of allowing any number of '@' characters in host
  // as long as they are not consecutive.
  return std::all_of(host.begin(), host.end(), valid_host_char) && !absl::StrContains(host, "@@") &&
         std::all_of(path_query.begin(), path_query.end(), is_valid_path_query_char);
}

// Returns true if `version_input` is a valid HTTP version string as defined at
// https://www.rfc-editor.org/rfc/rfc9112.html#section-2.3, or empty (for HTTP/0.9).
bool isVersionValid(absl::string_view version_input) {
  if (version_input.empty()) {
    return true;
  }

  if (!absl::StartsWith(version_input, kHttpVersionPrefix)) {
    return false;
  }
  version_input.remove_prefix(kHttpVersionPrefix.size());

  // Version number is in the form of "[0-9].[0-9]".
  return version_input.size() == 3 && absl::ascii_isdigit(version_input[0]) &&
         version_input[1] == '.' && absl::ascii_isdigit(version_input[2]);
}

bool isHeaderNameValid(absl::string_view name) {
  return std::all_of(name.begin(), name.end(), [](absl::string_view::value_type c) {
    return std::binary_search(kValidCharactersBegin, kValidCharactersEnd, c);
  });
}

} // anonymous namespace

BalsaParser::BalsaParser(MessageType type, ParserCallbacks* connection, size_t max_header_length,
                         bool enable_trailers, bool allow_custom_methods)
    : message_type_(type), connection_(connection), enable_trailers_(enable_trailers),
      allow_custom_methods_(allow_custom_methods) {
  ASSERT(connection_ != nullptr);

  quiche::HttpValidationPolicy http_validation_policy;
  http_validation_policy.disallow_header_continuation_lines = false;
  http_validation_policy.require_header_colon = true;
  http_validation_policy.disallow_multiple_content_length = true;
  http_validation_policy.disallow_transfer_encoding_with_content_length = false;
  http_validation_policy.validate_transfer_encoding = false;
  http_validation_policy.require_content_length_if_body_required = false;
  http_validation_policy.disallow_invalid_header_characters_in_response = true;
  http_validation_policy.disallow_lone_cr_in_chunk_extension = Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.http1_balsa_disallow_lone_cr_in_chunk_extension");
  framer_.set_http_validation_policy(http_validation_policy);

  framer_.set_balsa_headers(&headers_);
  framer_.set_balsa_visitor(this);
  framer_.set_max_header_length(max_header_length);
  framer_.set_invalid_chars_level(quiche::BalsaFrame::InvalidCharsLevel::kError);
  framer_.EnableTrailers();

  switch (message_type_) {
  case MessageType::Request:
    framer_.set_is_request(true);
    break;
  case MessageType::Response:
    framer_.set_is_request(false);
    break;
  }
}

size_t BalsaParser::execute(const char* slice, int len) {
  ASSERT(status_ != ParserStatus::Error);

  if (len > 0 && !first_byte_processed_) {
    if (delay_reset_) {
      if (first_message_) {
        first_message_ = false;
      } else {
        framer_.Reset();
      }
    }

    if (message_type_ == MessageType::Request && !allow_custom_methods_ &&
        !isFirstCharacterOfValidMethod(*slice)) {
      status_ = ParserStatus::Error;
      error_message_ = "HPE_INVALID_METHOD";
      return 0;
    }
    if (message_type_ == MessageType::Response && *slice != kResponseFirstByte) {
      status_ = ParserStatus::Error;
      error_message_ = "HPE_INVALID_CONSTANT";
      return 0;
    }

    status_ = convertResult(connection_->onMessageBegin());
    if (status_ == ParserStatus::Error) {
      return 0;
    }

    first_byte_processed_ = true;
  }

  if (len == 0 && headers_done_ && !isChunked() &&
      ((message_type_ == MessageType::Response && hasTransferEncoding()) ||
       !headers_.content_length_valid())) {
    MessageDone();
    return 0;
  }

  if (first_byte_processed_ && len == 0) {
    status_ = ParserStatus::Error;
    error_message_ = "HPE_INVALID_EOF_STATE";
    return 0;
  }

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

ParserStatus BalsaParser::getStatus() const { return status_; }

Http::Code BalsaParser::statusCode() const {
  return static_cast<Http::Code>(headers_.parsed_response_code());
}

bool BalsaParser::isHttp11() const {
  if (message_type_ == MessageType::Request) {
    return absl::EndsWith(headers_.first_line(), Http::Headers::get().ProtocolStrings.Http11String);
  } else {
    return absl::StartsWith(headers_.first_line(),
                            Http::Headers::get().ProtocolStrings.Http11String);
  }
}

absl::optional<uint64_t> BalsaParser::contentLength() const {
  if (!headers_.content_length_valid()) {
    return absl::nullopt;
  }
  return headers_.content_length();
}

bool BalsaParser::isChunked() const { return headers_.transfer_encoding_is_chunked(); }

absl::string_view BalsaParser::methodName() const { return headers_.request_method(); }

absl::string_view BalsaParser::errorMessage() const { return error_message_; }

int BalsaParser::hasTransferEncoding() const {
  return headers_.HasHeader(Http::Headers::get().TransferEncoding);
}

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
  validateAndProcessHeadersOrTrailersImpl(headers, /* trailers = */ false);
}
void BalsaParser::OnTrailers(std::unique_ptr<quiche::BalsaHeaders> trailers) {
  validateAndProcessHeadersOrTrailersImpl(*trailers, /* trailers = */ true);
}

void BalsaParser::OnRequestFirstLineInput(absl::string_view /*line_input*/,
                                          absl::string_view method_input,
                                          absl::string_view request_uri,
                                          absl::string_view version_input) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  if (!isMethodValid(method_input, allow_custom_methods_)) {
    status_ = ParserStatus::Error;
    error_message_ = "HPE_INVALID_METHOD";
    return;
  }
  const bool is_connect = method_input == Headers::get().MethodValues.Connect;
  if (!isUrlValid(request_uri, is_connect)) {
    status_ = ParserStatus::Error;
    error_message_ = "HPE_INVALID_URL";
    return;
  }
  if (!isVersionValid(version_input)) {
    status_ = ParserStatus::Error;
    error_message_ = "HPE_INVALID_VERSION";
    return;
  }
  status_ = convertResult(connection_->onUrl(request_uri.data(), request_uri.size()));
}

void BalsaParser::OnResponseFirstLineInput(absl::string_view /*line_input*/,
                                           absl::string_view version_input,
                                           absl::string_view /*status_input*/,
                                           absl::string_view reason_input) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  if (!isVersionValid(version_input)) {
    status_ = ParserStatus::Error;
    error_message_ = "HPE_INVALID_VERSION";
    return;
  }
  status_ = convertResult(connection_->onStatus(reason_input.data(), reason_input.size()));
}

void BalsaParser::OnChunkLength(size_t chunk_length) {
  if (status_ == ParserStatus::Error) {
    return;
  }
  const bool is_final_chunk = chunk_length == 0;
  connection_->onChunkHeader(is_final_chunk);
}

void BalsaParser::OnChunkExtensionInput(absl::string_view /*input*/) {}

void BalsaParser::OnInterimHeaders(std::unique_ptr<BalsaHeaders> /*headers*/) {}

void BalsaParser::HeaderDone() {
  if (status_ == ParserStatus::Error) {
    return;
  }
  headers_done_ = true;
  CallbackResult result = connection_->onHeadersComplete();
  status_ = convertResult(result);
  if (result == CallbackResult::NoBody || result == CallbackResult::NoBodyData) {
    MessageDone();
  }
}

void BalsaParser::ContinueHeaderDone() {}

void BalsaParser::MessageDone() {
  if (status_ == ParserStatus::Error) {
    return;
  }
  status_ = convertResult(connection_->onMessageComplete());
  if (!delay_reset_) {
    framer_.Reset();
  }
  first_byte_processed_ = false;
  headers_done_ = false;
}

void BalsaParser::HandleError(BalsaFrameEnums::ErrorCode error_code) {
  status_ = ParserStatus::Error;
  switch (error_code) {
  case BalsaFrameEnums::UNKNOWN_TRANSFER_ENCODING:
    error_message_ = "unsupported transfer encoding";
    break;
  case BalsaFrameEnums::INVALID_CHUNK_LENGTH:
    error_message_ = "HPE_INVALID_CHUNK_SIZE";
    break;
  case BalsaFrameEnums::HEADERS_TOO_LONG:
    error_message_ = "headers size exceeds limit";
    break;
  case BalsaFrameEnums::TRAILER_TOO_LONG:
    error_message_ = "trailers size exceeds limit";
    break;
  case BalsaFrameEnums::TRAILER_MISSING_COLON:
    error_message_ = "HPE_INVALID_HEADER_TOKEN";
    break;
  case BalsaFrameEnums::INVALID_HEADER_CHARACTER:
    error_message_ = "header value contains invalid chars";
    break;
  case BalsaFrameEnums::MULTIPLE_CONTENT_LENGTH_KEYS:
    error_message_ = "HPE_UNEXPECTED_CONTENT_LENGTH";
    break;
  default:
    error_message_ = BalsaFrameEnums::ErrorCodeToString(error_code);
  }
}

void BalsaParser::HandleWarning(BalsaFrameEnums::ErrorCode error_code) {
  if (error_code == BalsaFrameEnums::TRAILER_MISSING_COLON) {
    HandleError(error_code);
  }
}

void BalsaParser::validateAndProcessHeadersOrTrailersImpl(const quiche::BalsaHeaders& headers,
                                                          bool trailers) {
  for (const auto& [key, value] : headers.lines()) {
    if (status_ == ParserStatus::Error) {
      return;
    }

    if (!isHeaderNameValid(key)) {
      status_ = ParserStatus::Error;
      error_message_ = "HPE_INVALID_HEADER_TOKEN";
      return;
    }

    if (trailers && !enable_trailers_) {
      continue;
    }

    status_ = convertResult(connection_->onHeaderField(key.data(), key.length()));
    if (status_ == ParserStatus::Error) {
      return;
    }

    // Remove CR and LF characters to match http-parser behavior.
    auto is_cr_or_lf = [](char c) { return c == '\r' || c == '\n'; };
    if (std::any_of(value.begin(), value.end(), is_cr_or_lf)) {
      std::string value_without_cr_or_lf;
      value_without_cr_or_lf.reserve(value.size());
      for (char c : value) {
        if (!is_cr_or_lf(c)) {
          value_without_cr_or_lf.push_back(c);
        }
      }
      status_ = convertResult(connection_->onHeaderValue(value_without_cr_or_lf.data(),
                                                         value_without_cr_or_lf.length()));
    } else {
      // No need to copy if header value does not contain CR or LF.
      status_ = convertResult(connection_->onHeaderValue(value.data(), value.length()));
    }
  }
}

ParserStatus BalsaParser::convertResult(CallbackResult result) const {
  return result == CallbackResult::Error ? ParserStatus::Error : status_;
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
