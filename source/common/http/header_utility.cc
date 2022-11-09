#include "source/common/http/header_utility.h"

#include <array>

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/json_escape_string.h"
#include "source/common/common/matchers.h"
#include "source/common/common/regex.h"
#include "source/common/common/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {

struct SharedResponseCodeDetailsValues {
  const absl::string_view InvalidAuthority = "http.invalid_authority";
  const absl::string_view ConnectUnsupported = "http.connect_not_supported";
  const absl::string_view InvalidMethod = "http.invalid_method";
  const absl::string_view InvalidPath = "http.invalid_path";
  const absl::string_view InvalidScheme = "http.invalid_scheme";
};

using SharedResponseCodeDetails = ConstSingleton<SharedResponseCodeDetailsValues>;

// HeaderMatcher will consist of:
//   header_match_specifier which can be any one of exact_match, regex_match, range_match,
//   present_match, prefix_match or suffix_match.
//   Each of these also can be inverted with the invert_match option.
//   Absence of these options implies empty header value match based on header presence.
//   a.exact_match: value will be used for exact string matching.
//   b.regex_match: Match will succeed if header value matches the value specified here.
//   c.range_match: Match will succeed if header value lies within the range specified
//     here, using half open interval semantics [start,end).
//   d.present_match: Match will succeed if the header is present.
//   f.prefix_match: Match will succeed if header value matches the prefix value specified here.
//   g.suffix_match: Match will succeed if header value matches the suffix value specified here.
HeaderUtility::HeaderData::HeaderData(const envoy::config::route::v3::HeaderMatcher& config)
    : name_(config.name()), invert_match_(config.invert_match()) {
  switch (config.header_match_specifier_case()) {
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kExactMatch:
    header_match_type_ = HeaderMatchType::Value;
    value_ = config.exact_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSafeRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_ = Regex::Utility::parseRegex(config.safe_regex_match());
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kRangeMatch:
    header_match_type_ = HeaderMatchType::Range;
    range_.set_start(config.range_match().start());
    range_.set_end(config.range_match().end());
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPresentMatch:
    header_match_type_ = HeaderMatchType::Present;
    present_ = config.present_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPrefixMatch:
    header_match_type_ = HeaderMatchType::Prefix;
    value_ = config.prefix_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSuffixMatch:
    header_match_type_ = HeaderMatchType::Suffix;
    value_ = config.suffix_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kContainsMatch:
    header_match_type_ = HeaderMatchType::Contains;
    value_ = config.contains_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kStringMatch:
    header_match_type_ = HeaderMatchType::StringMatch;
    string_match_ =
        std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
            config.string_match());
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::
      HEADER_MATCH_SPECIFIER_NOT_SET:
    FALLTHRU;
  default:
    header_match_type_ = HeaderMatchType::Present;
    present_ = true;
    break;
  }
}

bool HeaderUtility::matchHeaders(const HeaderMap& request_headers,
                                 const std::vector<HeaderDataPtr>& config_headers) {
  // No headers to match is considered a match.
  if (!config_headers.empty()) {
    for (const HeaderDataPtr& cfg_header_data : config_headers) {
      if (!matchHeaders(request_headers, *cfg_header_data)) {
        return false;
      }
    }
  }

  return true;
}

HeaderUtility::GetAllOfHeaderAsStringResult
HeaderUtility::getAllOfHeaderAsString(const HeaderMap::GetResult& header_value,
                                      absl::string_view separator) {
  GetAllOfHeaderAsStringResult result;
  // In this case we concatenate all found headers using a delimiter before performing the
  // final match. We use an InlinedVector of absl::string_view to invoke the optimized join
  // algorithm. This requires a copying phase before we invoke join. The 3 used as the inline
  // size has been arbitrarily chosen.
  // TODO(mattklein123): Do we need to normalize any whitespace here?
  absl::InlinedVector<absl::string_view, 3> string_view_vector;
  string_view_vector.reserve(header_value.size());
  for (size_t i = 0; i < header_value.size(); i++) {
    string_view_vector.push_back(header_value[i]->value().getStringView());
  }
  result.result_backing_string_ = absl::StrJoin(string_view_vector, separator);

  return result;
}

HeaderUtility::GetAllOfHeaderAsStringResult
HeaderUtility::getAllOfHeaderAsString(const HeaderMap& headers, const Http::LowerCaseString& key,
                                      absl::string_view separator) {
  GetAllOfHeaderAsStringResult result;
  const auto header_value = headers.get(key);

  if (header_value.empty()) {
    // Empty for clarity. Avoid handling the empty case in the block below if the runtime feature
    // is disabled.
  } else if (header_value.size() == 1) {
    result.result_ = header_value[0]->value().getStringView();
  } else {
    return getAllOfHeaderAsString(header_value, separator);
  }

  return result;
}

bool HeaderUtility::matchHeaders(const HeaderMap& request_headers, const HeaderData& header_data) {
  const auto header_value = getAllOfHeaderAsString(request_headers, header_data.name_);

  if (!header_value.result().has_value()) {
    if (header_data.invert_match_) {
      return header_data.header_match_type_ == HeaderMatchType::Present && header_data.present_;
    } else {
      return header_data.header_match_type_ == HeaderMatchType::Present && !header_data.present_;
    }
  }

  const auto value = header_value.result().value();
  bool match;
  switch (header_data.header_match_type_) {
  case HeaderMatchType::Value:
    match = header_data.value_.empty() || value == header_data.value_;
    break;
  case HeaderMatchType::Regex:
    match = header_data.regex_->match(value);
    break;
  case HeaderMatchType::Range: {
    int64_t header_int_value = 0;
    match = absl::SimpleAtoi(value, &header_int_value) &&
            header_int_value >= header_data.range_.start() &&
            header_int_value < header_data.range_.end();
    break;
  }
  case HeaderMatchType::Present:
    match = header_data.present_;
    break;
  case HeaderMatchType::Prefix:
    match = absl::StartsWith(value, header_data.value_);
    break;
  case HeaderMatchType::Suffix:
    match = absl::EndsWith(value, header_data.value_);
    break;
  case HeaderMatchType::Contains:
    match = absl::StrContains(value, header_data.value_);
    break;
  case HeaderMatchType::StringMatch:
    match = header_data.string_match_->match(value);
    break;
  }

  return match != header_data.invert_match_;
}

bool HeaderUtility::schemeIsValid(const absl::string_view scheme) {
  return scheme == Headers::get().SchemeValues.Https || scheme == Headers::get().SchemeValues.Http;
}

bool HeaderUtility::headerValueIsValid(const absl::string_view header_value) {
  return nghttp2_check_header_value(reinterpret_cast<const uint8_t*>(header_value.data()),
                                    header_value.size()) != 0;
}

namespace {
// TODO(yanavlasov): This code is copied from the default UHV. As the transition to UHV is
// completed, replace it with the call to UHV.
constexpr bool testChar(const std::array<uint32_t, 8>& table, char c) {
  // CPU cache friendly version of a lookup in a bit table of size 256.
  // The table is organized as 8 32 bit words.
  // This function looks up a bit from the `table` at the index `c`.
  // This function is used to test whether a character `c` is allowed
  // or not based on the value of a bit at index `c`.
  uint8_t tmp = static_cast<uint8_t>(c);
  // The `tmp >> 5` determines which of the 8 uint32_t words has the bit at index `uc`.
  // The `0x80000000 >> (tmp & 0x1f)` determines the index of the bit within the 32 bit word.
  return (table[tmp >> 5] & (0x80000000 >> (tmp & 0x1f))) != 0;
}

// Header name character table.
// From RFC 9110, https://www.rfc-editor.org/rfc/rfc9110.html#section-5.1:
//
// SPELLCHECKER(off)
// header-field   = field-name ":" OWS field-value OWS
// field-name     = token
// token          = 1*tchar
//
// tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
//                / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
//                / DIGIT / ALPHA
// SPELLCHECKER(on)
constexpr std::array<uint32_t, 8> kGenericHeaderNameCharTable = {
    // control characters
    0b00000000000000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b01011111001101101111111111000000,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b01111111111111111111111111100011,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b11111111111111111111111111101010,
    // extended ascii
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
};

} // namespace

bool HeaderUtility::headerNameIsValid(const absl::string_view header_key) {
  if (!header_key.empty() && header_key[0] == ':') {
    // For HTTP/2 pseudo header, use the HTTP/2 semantics for checking validity
    return nghttp2_check_header_name(reinterpret_cast<const uint8_t*>(header_key.data()),
                                     header_key.size()) != 0;
  }
  // For all other header use HTTP/1 semantics. The only difference from HTTP/2 is that
  // uppercase characters are allowed. This allows HTTP filters to add header with mixed
  // case names. The HTTP/1 codec will send as is, as uppercase characters are allowed.
  // However the HTTP/2 codec will NOT convert these to lowercase when serializing the
  // header map, thus producing an invalid request.
  // TODO(yanavlasov): make validation in HTTP/2 case stricter.
  bool is_valid = true;
  for (auto iter = header_key.begin(); iter != header_key.end() && is_valid; ++iter) {
    is_valid &= testChar(kGenericHeaderNameCharTable, *iter);
  }
  return is_valid;
}

bool HeaderUtility::headerNameContainsUnderscore(const absl::string_view header_name) {
  return header_name.find('_') != absl::string_view::npos;
}

bool HeaderUtility::authorityIsValid(const absl::string_view header_value) {
  return nghttp2_check_authority(reinterpret_cast<const uint8_t*>(header_value.data()),
                                 header_value.size()) != 0;
}

bool HeaderUtility::isSpecial1xx(const ResponseHeaderMap& response_headers) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.proxy_102_103") &&
      (response_headers.Status()->value() == "102" ||
       response_headers.Status()->value() == "103")) {
    return true;
  }
  return response_headers.Status()->value() == "100";
}

bool HeaderUtility::isConnect(const RequestHeaderMap& headers) {
  return headers.Method() && headers.Method()->value() == Http::Headers::get().MethodValues.Connect;
}

bool HeaderUtility::isConnectResponse(const RequestHeaderMap* request_headers,
                                      const ResponseHeaderMap& response_headers) {
  return request_headers && isConnect(*request_headers) &&
         static_cast<Http::Code>(Http::Utility::getResponseStatus(response_headers)) ==
             Http::Code::OK;
}

bool HeaderUtility::requestShouldHaveNoBody(const RequestHeaderMap& headers) {
  return (headers.Method() &&
          (headers.Method()->value() == Http::Headers::get().MethodValues.Get ||
           headers.Method()->value() == Http::Headers::get().MethodValues.Head ||
           headers.Method()->value() == Http::Headers::get().MethodValues.Delete ||
           headers.Method()->value() == Http::Headers::get().MethodValues.Trace ||
           headers.Method()->value() == Http::Headers::get().MethodValues.Connect));
}

bool HeaderUtility::isEnvoyInternalRequest(const RequestHeaderMap& headers) {
  const HeaderEntry* internal_request_header = headers.EnvoyInternalRequest();
  return internal_request_header != nullptr &&
         internal_request_header->value() == Headers::get().EnvoyInternalRequestValues.True;
}

void HeaderUtility::stripTrailingHostDot(RequestHeaderMap& headers) {
  auto host = headers.getHostValue();
  // If the host ends in a period, remove it.
  auto dot_index = host.rfind('.');
  if (dot_index == std::string::npos) {
    return;
  } else if (dot_index == (host.size() - 1)) {
    host.remove_suffix(1);
    headers.setHost(host);
    return;
  }
  // If the dot is just before a colon, it must be preceding the port number.
  // IPv6 addresses may contain colons or dots, but the dot will never directly
  // precede the colon, so this check should be sufficient to detect a trailing port number.
  if (host[dot_index + 1] == ':') {
    headers.setHost(absl::StrCat(host.substr(0, dot_index), host.substr(dot_index + 1)));
  }
}

absl::optional<uint32_t> HeaderUtility::stripPortFromHost(RequestHeaderMap& headers,
                                                          absl::optional<uint32_t> listener_port) {
  const absl::string_view original_host = headers.getHostValue();
  const absl::string_view::size_type port_start = getPortStart(original_host);
  if (port_start == absl::string_view::npos) {
    return absl::nullopt;
  }
  const absl::string_view port_str = original_host.substr(port_start + 1);
  uint32_t port = 0;
  if (!absl::SimpleAtoi(port_str, &port)) {
    return absl::nullopt;
  }
  if (listener_port.has_value() && port != listener_port) {
    // We would strip ports only if it is specified and they are the same, as local port of the
    // listener.
    return absl::nullopt;
  }
  const absl::string_view host = original_host.substr(0, port_start);
  headers.setHost(host);
  return port;
}

absl::string_view::size_type HeaderUtility::getPortStart(absl::string_view host) {
  const absl::string_view::size_type port_start = host.rfind(':');
  if (port_start == absl::string_view::npos) {
    return absl::string_view::npos;
  }
  // According to RFC3986 v6 address is always enclosed in "[]". section 3.2.2.
  const auto v6_end_index = host.rfind(']');
  if (v6_end_index == absl::string_view::npos || v6_end_index < port_start) {
    if ((port_start + 1) > host.size()) {
      return absl::string_view::npos;
    }
    return port_start;
  }
  return absl::string_view::npos;
}

constexpr bool isInvalidToken(unsigned char c) {
  if (c == '!' || c == '|' || c == '~' || c == '*' || c == '+' || c == '-' || c == '.' ||
      // #, $, %, &, '
      (c >= '#' && c <= '\'') ||
      // [0-9]
      (c >= '0' && c <= '9') ||
      // [A-Z]
      (c >= 'A' && c <= 'Z') ||
      // ^, _, `, [a-z]
      (c >= '^' && c <= 'z')) {
    return false;
  }
  return true;
}

absl::optional<std::reference_wrapper<const absl::string_view>>
HeaderUtility::requestHeadersValid(const RequestHeaderMap& headers) {
  // Make sure the host is valid.
  if (headers.Host() && !HeaderUtility::authorityIsValid(headers.Host()->value().getStringView())) {
    return SharedResponseCodeDetails::get().InvalidAuthority;
  }
  if (headers.Method()) {
    absl::string_view method = headers.Method()->value().getStringView();
    if (method.empty() || std::any_of(method.begin(), method.end(), isInvalidToken)) {
      return SharedResponseCodeDetails::get().InvalidMethod;
    }
  }
  if (headers.Scheme() && absl::StrContains(headers.Scheme()->value().getStringView(), ",")) {
    return SharedResponseCodeDetails::get().InvalidScheme;
  }
  return absl::nullopt;
}

bool HeaderUtility::shouldCloseConnection(Http::Protocol protocol,
                                          const RequestOrResponseHeaderMap& headers) {
  // HTTP/1.0 defaults to single-use connections. Make sure the connection will be closed unless
  // Keep-Alive is present.
  if (protocol == Protocol::Http10 &&
      (!headers.Connection() ||
       !Envoy::StringUtil::caseFindToken(headers.Connection()->value().getStringView(), ",",
                                         Http::Headers::get().ConnectionValues.KeepAlive))) {
    return true;
  }

  if (protocol == Protocol::Http11 && headers.Connection() &&
      Envoy::StringUtil::caseFindToken(headers.Connection()->value().getStringView(), ",",
                                       Http::Headers::get().ConnectionValues.Close)) {
    return true;
  }

  // Note: Proxy-Connection is not a standard header, but is supported here
  // since it is supported by http-parser the underlying parser for http
  // requests.
  if (protocol < Protocol::Http2 && headers.ProxyConnection() &&
      Envoy::StringUtil::caseFindToken(headers.ProxyConnection()->value().getStringView(), ",",
                                       Http::Headers::get().ConnectionValues.Close)) {
    return true;
  }
  return false;
}

Http::Status HeaderUtility::checkRequiredRequestHeaders(const Http::RequestHeaderMap& headers) {
  if (!headers.Method()) {
    return absl::InvalidArgumentError(
        absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Method.get()));
  }
  bool is_connect = Http::HeaderUtility::isConnect(headers);
  if (is_connect) {
    if (!headers.Host()) {
      // Host header must be present for CONNECT request.
      return absl::InvalidArgumentError(
          absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Host.get()));
    }
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.validate_connect")) {
      if (headers.Path() && !headers.Protocol()) {
        // Path and Protocol header should only be present for CONNECT for upgrade style CONNECT.
        return absl::InvalidArgumentError(
            absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Protocol.get()));
      }
      if (!headers.Path() && headers.Protocol()) {
        // Path and Protocol header should only be present for CONNECT for upgrade style CONNECT.
        return absl::InvalidArgumentError(
            absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Path.get()));
      }
    }
  } else {
    if (!headers.Path()) {
      // :path header must be present for non-CONNECT requests.
      return absl::InvalidArgumentError(
          absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Path.get()));
    }
  }
  return Http::okStatus();
}

Http::Status HeaderUtility::checkValidRequestHeaders(const Http::RequestHeaderMap& headers) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.validate_upstream_headers")) {
    return Http::okStatus();
  }

  const HeaderEntry* invalid_entry = nullptr;
  bool invalid_key = false;
  headers.iterate([&invalid_entry, &invalid_key](const HeaderEntry& header) -> HeaderMap::Iterate {
    if (!HeaderUtility::headerNameIsValid(header.key().getStringView())) {
      invalid_entry = &header;
      invalid_key = true;
      return HeaderMap::Iterate::Break;
    }

    if (!HeaderUtility::headerValueIsValid(header.value().getStringView())) {
      invalid_entry = &header;
      invalid_key = false;
      return HeaderMap::Iterate::Break;
    }

    return HeaderMap::Iterate::Continue;
  });

  if (invalid_entry) {
    // The header key may contain non-printable characters. Escape the key so that the error
    // details can be safely presented.
    const absl::string_view key = invalid_entry->key().getStringView();
    uint64_t extra_length = JsonEscaper::extraSpace(key);
    const std::string escaped_key = JsonEscaper::escapeString(key, extra_length);

    return absl::InvalidArgumentError(
        absl::StrCat("invalid header ", invalid_key ? "name: " : "value for: ", escaped_key));
  }
  return Http::okStatus();
}

Http::Status HeaderUtility::checkRequiredResponseHeaders(const Http::ResponseHeaderMap& headers) {
  const absl::optional<uint64_t> status = Utility::getResponseStatusNoThrow(headers);
  if (!status.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("missing required header: ", Envoy::Http::Headers::get().Status.get()));
  }
  return Http::okStatus();
}

bool HeaderUtility::isRemovableHeader(absl::string_view header) {
  return (header.empty() || header[0] != ':') &&
         !absl::EqualsIgnoreCase(header, Headers::get().HostLegacy.get());
}

bool HeaderUtility::isModifiableHeader(absl::string_view header) {
  return (header.empty() || header[0] != ':') &&
         !absl::EqualsIgnoreCase(header, Headers::get().HostLegacy.get());
}

HeaderUtility::HeaderValidationResult HeaderUtility::checkHeaderNameForUnderscores(
    absl::string_view header_name,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action,
    Stats::Counter& dropped_headers_with_underscores,
    Stats::Counter& requests_rejected_with_underscores_in_headers) {
  if (headers_with_underscores_action == envoy::config::core::v3::HttpProtocolOptions::ALLOW ||
      !HeaderUtility::headerNameContainsUnderscore(header_name)) {
    return HeaderValidationResult::ACCEPT;
  }
  if (headers_with_underscores_action ==
      envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER) {
    ENVOY_LOG_MISC(debug, "Dropping header with invalid characters in its name: {}", header_name);
    dropped_headers_with_underscores.inc();
    return HeaderValidationResult::DROP;
  }
  ENVOY_LOG_MISC(debug, "Rejecting request due to header name with underscores: {}", header_name);
  requests_rejected_with_underscores_in_headers.inc();
  return HeaderUtility::HeaderValidationResult::REJECT;
}

HeaderUtility::HeaderValidationResult
HeaderUtility::validateContentLength(absl::string_view header_value,
                                     bool override_stream_error_on_invalid_http_message,
                                     bool& should_close_connection, size_t& content_length_output) {
  should_close_connection = false;
  std::vector<absl::string_view> values = absl::StrSplit(header_value, ',');
  absl::optional<uint64_t> content_length;
  for (const absl::string_view& value : values) {
    uint64_t new_value;
    if (!absl::SimpleAtoi(value, &new_value) ||
        !std::all_of(value.begin(), value.end(), absl::ascii_isdigit)) {
      ENVOY_LOG_MISC(debug, "Content length was either unparseable or negative");
      should_close_connection = !override_stream_error_on_invalid_http_message;
      return HeaderValidationResult::REJECT;
    }
    if (!content_length.has_value()) {
      content_length = new_value;
      continue;
    }
    if (new_value != content_length.value()) {
      ENVOY_LOG_MISC(
          debug,
          "Parsed content length {} is inconsistent with previously detected content length {}",
          new_value, content_length.value());
      should_close_connection = !override_stream_error_on_invalid_http_message;
      return HeaderValidationResult::REJECT;
    }
  }
  content_length_output = content_length.value();
  return HeaderValidationResult::ACCEPT;
}

std::vector<absl::string_view>
HeaderUtility::parseCommaDelimitedHeader(absl::string_view header_value) {
  std::vector<absl::string_view> values;
  for (absl::string_view s : absl::StrSplit(header_value, ',')) {
    absl::string_view token = absl::StripAsciiWhitespace(s);
    if (token.empty()) {
      continue;
    }
    values.emplace_back(token);
  }
  return values;
}

absl::string_view HeaderUtility::getSemicolonDelimitedAttribute(absl::string_view value) {
  return absl::StripAsciiWhitespace(StringUtil::cropRight(value, ";"));
}

std::string HeaderUtility::addEncodingToAcceptEncoding(absl::string_view accept_encoding_header,
                                                       absl::string_view encoding) {
  // Append the content encoding only if it isn't already present in the
  // accept_encoding header. If it is present with a q-value ("gzip;q=0.3"),
  // remove the q-value to indicate that the content encoding setting that we
  // add has max priority (i.e. q-value 1.0).
  std::vector<absl::string_view> newContentEncodings;
  std::vector<absl::string_view> contentEncodings =
      Http::HeaderUtility::parseCommaDelimitedHeader(accept_encoding_header);
  for (absl::string_view contentEncoding : contentEncodings) {
    absl::string_view strippedEncoding =
        Http::HeaderUtility::getSemicolonDelimitedAttribute(contentEncoding);
    if (strippedEncoding != encoding) {
      // Add back all content encodings back except for the content encoding that we want to
      // add. For example, if content encoding is "gzip", this filters out encodings "gzip" and
      // "gzip;q=0.6".
      newContentEncodings.push_back(contentEncoding);
    }
  }
  // Finally add a single instance of our content encoding.
  newContentEncodings.push_back(encoding);
  return absl::StrJoin(newContentEncodings, ",");
}

bool HeaderUtility::isPseudoHeader(absl::string_view header_name) {
  return !header_name.empty() && header_name[0] == ':';
}

} // namespace Http
} // namespace Envoy
