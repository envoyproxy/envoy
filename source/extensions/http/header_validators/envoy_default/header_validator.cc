#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

#include <charconv>

#include "envoy/http/header_validator_errors.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

namespace {

template <typename IntType>
std::from_chars_result fromChars(const absl::string_view string_value, IntType& value) {
  return std::from_chars(string_value.data(), string_value.data() + string_value.size(), value);
}

} // namespace

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::UhvResponseCodeDetail;

HeaderValidator::HeaderValidator(const HeaderValidatorConfig& config, Protocol protocol,
                                 ::Envoy::Http::HeaderValidatorStats& stats)
    : config_(config), protocol_(protocol), header_values_(::Envoy::Http::Headers::get()),
      stats_(stats), path_normalizer_(config) {}

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

  if (!absl::EqualsIgnoreCase(scheme, "http") && !absl::EqualsIgnoreCase(scheme, "https")) {
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
  auto result = fromChars(value_string_view, status_value);
  if (result.ec != std::errc() ||
      result.ptr != (value_string_view.data() + value_string_view.size())) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidStatus};
  }

  if (status_value < kMinimumResponseStatusCode || status_value > kMaximumResponseStatusCode) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidStatus};
  }

  return HeaderValueValidationResult::success();
}

HeaderValidator::HeaderEntryValidationResult
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
      stats_.incRequestsRejectedWithUnderscoresInHeaders();
      return {HeaderEntryValidationResult::Action::Reject,
              UhvResponseCodeDetail::get().InvalidUnderscore};
    } else if (underscore_action == HeaderValidatorConfig::DROP_HEADER) {
      stats_.incDroppedHeadersWithUnderscores();
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
  const auto value_string_view = value.getStringView();

  if (value_string_view.empty()) {
    return {HeaderValueValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidContentLength};
  }

  std::uint64_t int_value{};
  auto result = fromChars(value_string_view, int_value);
  if (result.ec != std::errc() ||
      result.ptr != (value_string_view.data() + value_string_view.size())) {
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
      auto result = fromChars(port_string.substr(1), port_int);
      if (result.ec != std::errc() || result.ptr != (port_string.data() + port_string.size()) ||
          port_int == 0) {
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

HeaderValidator::HeaderEntryValidationResult HeaderValidator::validateGenericRequestHeaderEntry(
    const ::Envoy::Http::HeaderString& key, const ::Envoy::Http::HeaderString& value,
    const HeaderValidatorMap& protocol_specific_header_validators) {
  const auto& key_string_view = key.getStringView();
  if (key_string_view.empty()) {
    // reject empty header names
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().EmptyHeaderName};
  }

  // Protocol specific header validators use this map to check protocol specific headers. For
  // example the transfer-encoding header checks are different for H/1 and H/2 or H/3.
  // This map also contains validation methods for headers that have additional restrictions other
  // than the generic character set (such as :method). The headers that are not part of this map,
  // just need the character set validation.
  auto validator_it = protocol_specific_header_validators.find(key_string_view);
  if (validator_it != protocol_specific_header_validators.end()) {
    const auto& validator = validator_it->second;
    return validator(value);
  }

  if (key_string_view.at(0) != ':') {
    // Validate the (non-pseudo) header name
    auto name_result = validateGenericHeaderName(key);
    if (!name_result) {
      return name_result;
    }
  } else {
    // header_validator_map contains every known pseudo header. If the header name starts with ":"
    // and we don't have a validator registered in the map, then the header name is an unknown
    // pseudo header.
    return {HeaderEntryValidationResult::Action::Reject,
            UhvResponseCodeDetail::get().InvalidPseudoHeader};
  }

  return validateGenericHeaderValue(value);
}

// For all (H/1, H/2 and H/3) protocols, trailers should only contain generic headers. As such a
// common validation method can be used.
// More in depth explanation for using common function:
// For H/2 (and so H/3), per
// https://www.rfc-editor.org/rfc/rfc9113#section-8.1 trailers MUST NOT contain pseudo header
// fields.
// For H/1 the codec will never produce H/2 pseudo headers and per
// https://www.rfc-editor.org/rfc/rfc9110#section-6.5 there are no other prohibitions.
// As a result this common function can cover trailer validation for all protocols.
HeaderValidator::TrailerValidationResult
HeaderValidator::validateTrailers(::Envoy::Http::HeaderMap& trailers) {
  std::string reject_details;
  std::vector<absl::string_view> drop_headers;
  trailers.iterate(
      [this, &reject_details, &drop_headers](
          const ::Envoy::Http::HeaderEntry& header_entry) -> ::Envoy::Http::HeaderMap::Iterate {
        const auto& header_name = header_entry.key();
        const auto& header_value = header_entry.value();

        auto entry_name_result = validateGenericHeaderName(header_name);
        if (entry_name_result.action() == HeaderEntryValidationResult::Action::DropHeader) {
          // drop the header, continue processing the request
          drop_headers.push_back(header_name.getStringView());
        } else if (!entry_name_result) {
          reject_details = static_cast<std::string>(entry_name_result.details());
        } else {
          auto entry_value_result = validateGenericHeaderValue(header_value);
          if (!entry_value_result) {
            reject_details = static_cast<std::string>(entry_value_result.details());
          }
        }

        return reject_details.empty() ? ::Envoy::Http::HeaderMap::Iterate::Continue
                                      : ::Envoy::Http::HeaderMap::Iterate::Break;
      });

  if (!reject_details.empty()) {
    return {TrailerValidationResult::Action::Reject, reject_details};
  }

  for (auto& name : drop_headers) {
    trailers.remove(::Envoy::Http::LowerCaseString(name));
  }

  return TrailerValidationResult::success();
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
