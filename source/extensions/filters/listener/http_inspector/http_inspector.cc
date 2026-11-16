#include "source/extensions/filters/listener/http_inspector/http_inspector.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

namespace {

// RFC 7230 token characters. HTTP request methods must consist solely of these.
constexpr absl::string_view kValidHttpMethodChars =
    "!#$%&'*+-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ^_`abcdefghijklmnopqrstuvwxyz|~";

constexpr size_t kMaxHttpMethodLength = 32;

// The HTTP-version word of a request line is exactly "HTTP/x.y" (RFC 9112
// section 2.3), or absent for a HTTP/0.9-style simple request.
constexpr absl::string_view kHttpVersionPrefix = "HTTP/";
constexpr size_t kHttpVersionLength = 8;

// The HTTP/1 parser treats every byte <= ' ' as a first-line separator.
constexpr bool isFirstLineWhitespace(char c) { return static_cast<uint8_t>(c) <= ' '; }

// Characters a CONNECT request-target (authority-form host) may start with,
// mirroring the host characters the HTTP/1 request-target validation accepts.
bool isValidConnectTargetChar(char c) {
  return absl::ascii_isalnum(c) || c == '!' || c == '$' || c == '%' || c == '&' || c == '\'' ||
         c == '(' || c == ')' || c == '*' || c == '+' || c == ',' || c == '-' || c == '.' ||
         c == ':' || c == ';' || c == '=' || c == '@' || c == '[' || c == ']' || c == '_' ||
         c == '~' || c == '?';
}

// Whether character `c` at offset `offset` of the version word can still be
// part of a "HTTP/x.y" version.
bool isValidVersionChar(size_t offset, char c) {
  if (offset < kHttpVersionPrefix.size()) {
    return c == kHttpVersionPrefix[offset];
  }
  if (offset == kHttpVersionPrefix.size() + 1) {
    return c == '.';
  }
  return offset < kHttpVersionLength && absl::ascii_isdigit(c);
}

} // namespace

Config::Config(Stats::Scope& scope)
    : stats_{ALL_HTTP_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "http_inspector."))} {}

const absl::string_view Filter::HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

Filter::Filter(const ConfigSharedPtr config)
    : config_(config), no_op_callbacks_(),
      requested_read_bytes_(Config::DEFAULT_INITIAL_BUFFER_SIZE) {
  // Filter for only Request Message types with NoOp Parser callbacks.
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_inspector_use_balsa_parser")) {
    // Set both allow_custom_methods and enable_trailers to true with BalsaParser.
    parser_ = std::make_unique<Http::Http1::BalsaParser>(
        Http::Http1::MessageType::Request, &no_op_callbacks_, Config::MAX_INSPECT_SIZE + 1024, true,
        true);
  } else {
    parser_ = std::make_unique<Http::Http1::LegacyHttpParserImpl>(Http::Http1::MessageType::Request,
                                                                  &no_op_callbacks_);
  }
}

Network::FilterStatus Filter::onData(Network::ListenerFilterBuffer& buffer) {
  auto raw_slice = buffer.rawSlice();
  const char* buf = static_cast<const char*>(raw_slice.mem_);
  const auto parse_state = parseHttpHeader(absl::string_view(buf, raw_slice.len_));
  switch (parse_state) {
  case ParseState::Error:
    // Invalid HTTP preface found, then just continue for next filter.
    done(false);
    return Network::FilterStatus::Continue;
  case ParseState::Done:
    done(true);
    return Network::FilterStatus::Continue;
  case ParseState::Continue:
    ENVOY_LOG(trace, "http inspector: need more bytes");

    // If we have requested the maximum amount of data, then close the connection
    // the request line is too large to determine the http version.
    if (static_cast<size_t>(nread_) >= Config::MAX_INSPECT_SIZE) {
      ENVOY_LOG(warn, "http inspector: reached max buffer without determining HTTP version, "
                      "dropping connection");
      config_->stats().read_error_.inc();
      cb_->socket().ioHandle().close();
      return Network::FilterStatus::StopIteration;
    }

    // Otherwise, double the buffer size and try again
    if (static_cast<size_t>(nread_) >= requested_read_bytes_) {
      requested_read_bytes_ =
          std::min<uint32_t>(2 * requested_read_bytes_, Config::MAX_INSPECT_SIZE);
    }

    return Network::FilterStatus::StopIteration;
  }
  PANIC_DUE_TO_CORRUPT_ENUM
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(trace, "http inspector: new connection accepted");

  const Network::ConnectionSocket& socket = cb.socket();

  const absl::string_view transport_protocol = socket.detectedTransportProtocol();
  if (!transport_protocol.empty() && transport_protocol != "raw_buffer") {
    ENVOY_LOG(trace, "http inspector: cannot inspect http protocol with transport socket {}",
              transport_protocol);
    return Network::FilterStatus::Continue;
  }

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

ParseState Filter::parseHttpHeader(absl::string_view data) {
  const size_t len = std::min(data.length(), Filter::HTTP2_CONNECTION_PREFACE.length());
  if (Filter::HTTP2_CONNECTION_PREFACE.compare(0, len, data, 0, len) == 0) {
    if (data.length() < Filter::HTTP2_CONNECTION_PREFACE.length()) {
      return ParseState::Continue;
    }
    ENVOY_LOG(trace, "http inspector: http2 connection preface found");
    protocol_ = "HTTP/2";
    return ParseState::Done;
  } else {
    ASSERT(!data.empty());
    // Ensure first line (also request line for HTTP request) in the buffer is not empty.
    if (data[0] == '\r' || data[0] == '\n') {
      return ParseState::Error;
    }

    // Fast-fail when the request-line prefix can no longer be completed into
    // a valid request line of the form `method SP+ request-target (SP+
    // HTTP-version)?` (RFC 9112 section 3). The parser treats every byte
    // <= ' ' as a first-line separator and defers validation until a line
    // terminator is seen, so a non-HTTP connection whose early bytes look
    // like `token whitespace` — for example an `LDAP` bind request
    // `30 0c 02 01 01 60 ...` — otherwise stalls here until the listener
    // filter timeout. The scan is bounded to the first
    // kMaxHttpMethodLength bytes of the line; a prefix that can still be
    // completed into a valid request line is left to the parser.
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.http_inspector_fast_fail_invalid_method_bytes")) {
      const size_t line_end = data.find('\n');
      const absl::string_view prefix = data.substr(0, std::min(line_end, kMaxHttpMethodLength));
      const size_t bad = prefix.find_first_not_of(kValidHttpMethodChars);
      if (bad != absl::string_view::npos) {
        // The method must be non-empty and terminated by ASCII whitespace.
        if (bad == 0 || !absl::ascii_isspace(prefix[bad])) {
          return ParseState::Error;
        }
        // Skip the separator run after the method token.
        size_t pos = bad;
        while (pos < prefix.size() && isFirstLineWhitespace(prefix[pos])) {
          ++pos;
        }
        if (pos < prefix.size()) {
          // The next word is the request-target: it begins with '/', '*',
          // an alpha (scheme) character, or an authority host character for
          // CONNECT.
          const bool is_connect = prefix.substr(0, bad) == "CONNECT";
          const char first = prefix[pos];
          if (first != '/' && first != '*' &&
              !(is_connect ? isValidConnectTargetChar(first) : absl::ascii_isalpha(first))) {
            return ParseState::Error;
          }
          // The rest of the request-target may only contain printable
          // characters.
          while (pos < prefix.size() && !isFirstLineWhitespace(prefix[pos])) {
            if (static_cast<uint8_t>(prefix[pos]) >= 0x7f) {
              return ParseState::Error;
            }
            ++pos;
          }
          // Skip the separator run before the optional version word.
          while (pos < prefix.size() && isFirstLineWhitespace(prefix[pos])) {
            ++pos;
          }
          if (pos < prefix.size()) {
            // A version word must match "HTTP/x.y".
            size_t version_len = 0;
            while (pos < prefix.size() && !isFirstLineWhitespace(prefix[pos])) {
              if (!isValidVersionChar(version_len, prefix[pos])) {
                return ParseState::Error;
              }
              ++version_len;
              ++pos;
            }
            // The version word is complete once a separator follows it or
            // the scan reaches the line terminator; a complete word must be
            // exactly "HTTP/x.y" and the last word of the request line.
            if (pos < prefix.size() || prefix.size() == line_end) {
              if (version_len != kHttpVersionLength) {
                return ParseState::Error;
              }
              while (pos < prefix.size() && isFirstLineWhitespace(prefix[pos])) {
                ++pos;
              }
              if (pos < prefix.size()) {
                return ParseState::Error;
              }
            }
          }
        }
      }
    }

    absl::string_view new_data = data.substr(nread_);
    const size_t pos = new_data.find_first_of('\n');
    if (pos != absl::string_view::npos) {
      // Include \n
      new_data = new_data.substr(0, pos + 1);

      ssize_t rc = parser_->execute(new_data.data(), new_data.length());
      nread_ += rc;
      ENVOY_LOG(trace, "http inspector: http_parser parsed {} chars, error code: {}", rc,
                parser_->errorMessage());

      // Errors in parsing HTTP.
      if (parser_->getStatus() != Http::Http1::ParserStatus::Ok &&
          parser_->getStatus() != Http::Http1::ParserStatus::Paused) {
        return ParseState::Error;
      }

      if (parser_->isHttp11()) {
        protocol_ = Http::Headers::get().ProtocolStrings.Http11String;
      } else {
        // Set other HTTP protocols to HTTP/1.0
        protocol_ = Http::Headers::get().ProtocolStrings.Http10String;
      }

      return ParseState::Done;
    } else {
      ssize_t rc = parser_->execute(new_data.data(), new_data.length());
      nread_ += rc;
      ENVOY_LOG(trace, "http inspector: http_parser parsed {} chars, error code: {}", rc,
                parser_->errorMessage());

      // Errors in parsing HTTP.
      if (parser_->getStatus() != Http::Http1::ParserStatus::Ok &&
          parser_->getStatus() != Http::Http1::ParserStatus::Paused) {
        return ParseState::Error;
      } else {
        return ParseState::Continue;
      }
    }
  }
}

void Filter::done(bool success) {
  ENVOY_LOG(trace, "http inspector: done: {}", success);

  if (success) {
    absl::string_view protocol;
    if (protocol_ == Http::Headers::get().ProtocolStrings.Http10String) {
      config_->stats().http10_found_.inc();
      protocol = Http::Utility::AlpnNames::get().Http10;
    } else if (protocol_ == Http::Headers::get().ProtocolStrings.Http11String) {
      config_->stats().http11_found_.inc();
      protocol = Http::Utility::AlpnNames::get().Http11;
    } else {
      ASSERT(protocol_ == "HTTP/2");
      config_->stats().http2_found_.inc();
      // h2 HTTP/2 over TLS, h2c HTTP/2 over TCP
      // TODO(yxue): use detected protocol from http inspector and support h2c token in HCM
      protocol = Http::Utility::AlpnNames::get().Http2c;
    }
    ENVOY_LOG(debug, "http inspector: set application protocol to {}", protocol);

    cb_->socket().setRequestedApplicationProtocols({protocol});
  } else {
    config_->stats().http_not_found_.inc();
  }
}

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
