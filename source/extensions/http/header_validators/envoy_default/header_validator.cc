#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

#include <charconv>

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;

HeaderValidator::HeaderValidator(const HeaderValidatorConfig& config, Protocol protocol,
                                 StreamInfo::StreamInfo& stream_info)
    : config_(config), protocol_(protocol), stream_info_(stream_info),
      header_values_(::Envoy::Http::Headers::get()) {}

HeaderValidator::HeaderValueValidationResult
HeaderValidator::validateMethodHeader(const HeaderString& value) {
  // HTTP Method Registry, from iana.org:
  // source: https://www.iana.org/assignments/http-methods/http-methods.xhtml
  //
  // From the RFC:
  //
  // tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "."
  //       /  "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
  // token = 1*tchar
  // method = token
  static absl::node_hash_set<absl::string_view> kHttpMethodRegistry = {
      "ACL",
      "BASELINE-CONTROL",
      "BIND",
      "CHECKIN",
      "CHECKOUT",
      "CONNECT",
      "COPY",
      "DELETE",
      "GET",
      "HEAD",
      "LABEL",
      "LINK",
      "LOCK",
      "MERGE",
      "MKACTIVITY",
      "MKCALENDAR",
      "MKCOL",
      "MKREDIRECTREF",
      "MKWORKSPACE",
      "MOVE",
      "OPTIONS",
      "ORDERPATCH",
      "PATCH",
      "POST",
      "PRI",
      "PROPFIND",
      "PROPPATCH",
      "PUT",
      "REBIND",
      "REPORT",
      "SEARCH",
      "TRACE",
      "UNBIND",
      "UNCHECKOUT",
      "UNLINK",
      "UNLOCK",
      "UPDATE",
      "UPDATEREDIRECTREF",
      "VERSION-CONTROL",
      "*",
  };

  const auto& method = value.getStringView();
  bool is_valid = true;

  if (config_.restrict_http_methods()) {
    is_valid = kHttpMethodRegistry.contains(method);
  } else {
    is_valid = !method.empty();
    for (auto iter = method.begin(); iter != method.end() && is_valid; ++iter) {
      is_valid &= testChar(kMethodHeaderCharTable, *iter);
    }
  }

  if (!is_valid) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidMethod};
  }

  return HeaderValueValidationResult::success();
}

HeaderValidator::HeaderValueValidationResult
HeaderValidator::validateSchemeHeader(const HeaderString& value) {
  // From RFC 3986, https://datatracker.ietf.org/doc/html/rfc3986#section-3.1:
  //
  // scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
  //
  // Although schemes are case-insensitive, the canonical form is lowercase and documents that
  // specify schemes must do so with lowercase letters. An implementation should accept uppercase
  // letters as equivalent to lowercase in scheme names (e.g., allow "HTTP" as well as "http") for
  // the sake of robustness but should only produce lowercase scheme names for consistency.
  //
  // The validation mode controls whether uppercase letters are permitted.
  absl::string_view scheme = value.getStringView();

  if (scheme != "http" && scheme != "https") {
    // TODO(#23313) - Honor config setting for mixed case.
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidScheme};
  }

  return HeaderValueValidationResult::success();
}

HeaderValidator::HeaderValueValidationResult
HeaderValidator::validateStatusHeader(const HeaderString& value) {
  // Validate that the response :status header is a valid whole number between 100 and 999
  // (inclusive). This is based on RFC 9110, although the Envoy implementation is more permissive
  // and allows status codes larger than 599,
  // https://www.rfc-editor.org/rfc/rfc9110.html#section-15:
  //
  // The status code of a response is a three-digit integer code that describes the result of the
  // request and the semantics of the response, including whether the request was successful and
  // what content is enclosed (if any). All valid status codes are within the range of 100 to 599,
  // inclusive.

  static uint32_t kMinimumResponseStatusCode = 100;
  static uint32_t kMaximumResponseStatusCode = 999;
  const auto& value_string_view = value.getStringView();

  // Convert the status to an integer.
  std::uint32_t status_value{};
  auto result = std::from_chars(value_string_view.begin(), value_string_view.end(), status_value);
  if (result.ec != std::errc() || result.ptr != value_string_view.end()) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidStatus};
  }

  if (status_value < kMinimumResponseStatusCode || status_value > kMaximumResponseStatusCode) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidStatus};
  }

  return HeaderValueValidationResult::success();
}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
HeaderValidator::validateGenericHeaderName(const HeaderString& name) {
  // Verify that the header name is valid. This also honors the underscore in
  // header configuration setting.
  //
  // From RFC 9110, https://www.rfc-editor.org/rfc/rfc9110.html#section-5.1:
  //
  // header-field   = field-name ":" OWS field-value OWS
  // field-name     = token
  // token          = 1*tchar
  //
  // tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
  //                / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
  //                / DIGIT / ALPHA
  //                ; any VCHAR, except delimiters
  const auto& key_string_view = name.getStringView();
  // This header name is initially invalid if the name is empty.
  if (key_string_view.empty()) {
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().EmptyHeaderName};
  }

  bool is_valid = true;
  bool has_underscore = false;
  char c = '\0';
  const auto& underscore_action = config_.headers_with_underscores_action();

  for (auto iter = key_string_view.begin(); iter != key_string_view.end() && is_valid; ++iter) {
    c = *iter;
    if (c != '_') {
      is_valid &= testChar(kGenericHeaderNameCharTable, c);
    } else {
      has_underscore = true;
    }
  }

  if (!is_valid) {
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidNameCharacters};
  }

  if (has_underscore) {
    if (underscore_action == HeaderValidatorConfig::REJECT_REQUEST) {
      return {HeaderEntryValidationResult::Action::Reject,
              UhvResponseCodeDetail::get().InvalidUnderscore};
    } else if (underscore_action == HeaderValidatorConfig::DROP_HEADER) {
      return {HeaderEntryValidationResult::Action::DropHeader,
              UhvResponseCodeDetail::get().InvalidUnderscore};
    }
  }

  return HeaderEntryValidationResult::success();
}

HeaderValidator::HeaderValueValidationResult
HeaderValidator::validateGenericHeaderValue(const HeaderString& value) {
  // Verify that the header value is valid.
  //
  // From RFC 9110, https://www.rfc-editor.org/rfc/rfc9110.html#section-5.5:
  //
  // header-field   = field-name ":" OWS field-value OWS
  // field-value    = *field-content
  // field-content  = field-vchar
  //                  [ 1*( SP / HTAB / field-vchar ) field-vchar ]
  // field-vchar    = VCHAR / obs-text
  // obs-text       = %x80-FF
  //
  // VCHAR          =  %x21-7E
  //                   ; visible (printing) characters
  const auto& value_string_view = value.getStringView();
  bool is_valid = true;

  for (auto iter = value_string_view.begin(); iter != value_string_view.end() && is_valid; ++iter) {
    is_valid &= testChar(kGenericHeaderValueCharTable, *iter);
  }

  if (!is_valid) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidValueCharacters};
  }

  return HeaderValueValidationResult::success();
}

HeaderValidator::HeaderValueValidationResult
HeaderValidator::validateContentLengthHeader(const HeaderString& value) {
  // From RFC 9110, https://www.rfc-editor.org/rfc/rfc9110.html#section-8.6:
  //
  // Content-Length = 1*DIGIT
  // TODO(#23315) - Validate multiple Content-Length values
  const auto& value_string_view = value.getStringView();

  if (value_string_view.empty()) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidContentLength};
  }

  std::uint64_t int_value{};
  auto result = std::from_chars(value_string_view.begin(), value_string_view.end(), int_value);
  if (result.ec != std::errc() || result.ptr != value_string_view.end()) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidContentLength};
  }

  return HeaderValueValidationResult::success();
}

HeaderValidator::HeaderValueValidationResult
HeaderValidator::validateHostHeader(const HeaderString& value) {
  // From RFC 9110, https://www.rfc-editor.org/rfc/rfc9110.html#section-7.2,
  // and RFC 3986, https://datatracker.ietf.org/doc/html/rfc3986#section-3.2.2:
  //
  // Host       = uri-host [ ":" port ]
  // uri-host   = IP-literal / IPv4address / reg-name
  //
  // TODO(#22859, #23314) - Fully implement IPv6 address validation
  const auto host = value.getStringView();
  if (host.empty()) {
    return {HeaderValueValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidHost};
  }

  // Check if the host/:authority contains the deprecated userinfo component. This is based on RFC
  // 9110, https://www.rfc-editor.org/rfc/rfc9110.html#section-4.2.4:
  //
  // Before making use of an "http" or "https" URI reference received from an untrusted source, a
  // recipient SHOULD parse for userinfo and treat its presence as an error; it is likely being
  // used to obscure the authority for the sake of phishing attacks.
  auto user_info_delimiter = host.find('@');
  if (user_info_delimiter != absl::string_view::npos) {
    // :authority cannot contain user info, reject the header
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo};
  }

  // Determine if the host is in IPv4, reg-name, or IPv6 form.
  auto result = host.at(0) == '[' ? validateHostHeaderIPv6(host) : validateHostHeaderRegName(host);
  if (!result.ok()) {
    return {HeaderValueValidationResult::Action::Reject, result.details()};
  }

  const auto port_string = result.portAndDelimiter();
  if (!port_string.empty()) {
    // Validate the port, which will be in the form of ":<uint16_t>"
    bool is_valid = true;
    if (port_string.at(0) != ':') {
      // The port must begin with ":"
      is_valid = false;
    } else {
      // parse the port number
      std::uint16_t port_int{};
      auto result = std::from_chars(std::next(port_string.begin()), port_string.end(), port_int);

      if (result.ec != std::errc() || result.ptr != port_string.end() || port_int == 0) {
        is_valid = false;
      }
    }

    if (!is_valid) {
      return {HeaderValueValidationResult::Action::Reject,
              UhvResponseCodeDetail::get().InvalidHost};
    }
  }

  return HeaderValueValidationResult::success();
}

HeaderValidator::HostHeaderValidationResult
HeaderValidator::validateHostHeaderIPv6(absl::string_view host) {
  // Validate an IPv6 address host header value. This is a simplified check based on RFC 3986,
  // https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2, that only validates the characters,
  // not the syntax of the address.

  // Validate that the address is enclosed between "[" and "]".
  std::size_t closing_bracket = host.rfind(']');
  if (host.empty() || host.at(0) != '[' || closing_bracket == absl::string_view::npos) {
    return HostHeaderValidationResult::reject(UhvResponseCodeDetail::get().InvalidHost);
  }

  // Get the address substring between the brackets.
  const auto address = host.substr(1, closing_bracket - 1);
  // Get the trailing port substring
  const auto port_string = host.substr(closing_bracket + 1);
  // Validate the IPv6 address characters
  bool is_valid = !address.empty();
  for (auto iter = address.begin(); iter != address.end() && is_valid; ++iter) {
    is_valid &= testChar(kHostIPv6AddressCharTable, *iter);
  }

  if (!is_valid) {
    return HostHeaderValidationResult::reject(UhvResponseCodeDetail::get().InvalidHost);
  }

  return HostHeaderValidationResult::success(address, port_string);
}

HeaderValidator::HostHeaderValidationResult
HeaderValidator::validateHostHeaderRegName(absl::string_view host) {
  // Validate a reg-name address host header value. This is a simplified check based on RFC 3986,
  // https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2, that only validates the characters,
  // not the syntax of the address.

  // Identify the port trailer
  auto port_delimiter = host.find(':');
  const auto address = host.substr(0, port_delimiter);
  bool is_valid = !address.empty();

  // Validate the reg-name characters
  for (auto iter = address.begin(); iter != address.end() && is_valid; ++iter) {
    is_valid &= testChar(kHostRegNameCharTable, *iter);
  }

  if (!is_valid) {
    return HostHeaderValidationResult::reject(UhvResponseCodeDetail::get().InvalidHost);
  }

  const auto port_string =
      port_delimiter != absl::string_view::npos ? host.substr(port_delimiter) : absl::string_view();
  return HostHeaderValidationResult::success(address, port_string);
}

HeaderValidator::HeaderValueValidationResult
HeaderValidator::validatePathHeaderCharacters(const HeaderString& value) {
  const auto& path = value.getStringView();
  bool is_valid = !path.empty();

  auto iter = path.begin();
  auto end = path.end();
  // Validate the path component of the URI
  for (; iter != end && is_valid; ++iter) {
    const char ch = *iter;
    if (ch == '?' || ch == '#') {
      // This is the start of the query or fragment portion of the path which uses a different
      // character table.
      break;
    }

    is_valid &= testChar(kPathHeaderCharTable, ch);
  }

  if (is_valid && iter != end && *iter == '?') {
    // Validate the query component of the URI
    ++iter;
    for (; iter != end && is_valid; ++iter) {
      const char ch = *iter;
      if (ch == '#') {
        break;
      }

      is_valid &= testChar(kUriQueryAndFragmentCharTable, ch);
    }
  }

  if (is_valid && iter != end && *iter == '#') {
    // Validate the fragment component of the URI
    ++iter;
    for (; iter != end && is_valid; ++iter) {
      is_valid &= testChar(kUriQueryAndFragmentCharTable, *iter);
    }
  }

  if (!is_valid) {
    return {HeaderValueValidationResult::Action::Reject, UhvResponseCodeDetail::get().InvalidUrl};
  }

  return HeaderValueValidationResult::success();
}

bool HeaderValidator::hasChunkedTransferEncoding(const HeaderString& value) {
  const auto encoding = value.getStringView();
  for (const auto token : StringUtil::splitToken(encoding, ",", true, true)) {
    if (absl::EqualsIgnoreCase(token, header_values_.TransferEncodingValues.Chunked)) {
      return true;
    }
  }

  return false;
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
