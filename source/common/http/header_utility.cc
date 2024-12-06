#include "source/common/http/header_utility.h"

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/json_escape_string.h"
#include "source/common/common/matchers.h"
#include "source/common/common/regex.h"
#include "source/common/common/utility.h"
#include "source/common/http/character_set_validation.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"

#ifdef ENVOY_NGHTTP2
#include "nghttp2/nghttp2.h"
#endif
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "quiche/common/structured_headers.h"
#endif
#include "quiche/http2/adapter/header_validator.h"

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

bool HeaderUtility::matchHeaders(const HeaderMap& request_headers,
                                 const std::vector<HeaderDataPtr>& config_headers) {
  // No headers to match is considered a match.
  if (!config_headers.empty()) {
    for (const HeaderDataPtr& cfg_header_data : config_headers) {
      if (!cfg_header_data->matchesHeaders(request_headers)) {
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

HeaderUtility::HeaderDataPtr
HeaderUtility::createHeaderData(const envoy::config::route::v3::HeaderMatcher& config,
                                Server::Configuration::CommonFactoryContext& factory_context) {
  switch (config.header_match_specifier_case()) {
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kExactMatch:
    return std::make_unique<HeaderDataExactMatch>(config);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSafeRegexMatch:
    return THROW_OR_RETURN_VALUE(HeaderDataRegexMatch::create(config, factory_context),
                                 std::unique_ptr<HeaderDataRegexMatch>);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kRangeMatch:
    return std::make_unique<HeaderDataRangeMatch>(config);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPresentMatch:
    return std::make_unique<HeaderDataPresentMatch>(config);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPrefixMatch:
    return std::make_unique<HeaderDataPrefixMatch>(config);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSuffixMatch:
    return std::make_unique<HeaderDataSuffixMatch>(config);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kContainsMatch:
    return std::make_unique<HeaderDataContainsMatch>(config);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kStringMatch:
    return std::make_unique<HeaderDataStringMatch>(config, factory_context);
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::
      HEADER_MATCH_SPECIFIER_NOT_SET:
    FALLTHRU;
  default:
    return std::make_unique<HeaderDataPresentMatch>(config, true);
    break;
  }
}

bool HeaderUtility::headerValueIsValid(const absl::string_view header_value) {
  return http2::adapter::HeaderValidator::IsValidHeaderValue(header_value,
                                                             http2::adapter::ObsTextOption::kAllow);
}

bool HeaderUtility::headerNameIsValid(absl::string_view header_key) {
  if (!header_key.empty() && header_key[0] == ':') {
#ifdef ENVOY_NGHTTP2
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.sanitize_http2_headers_without_nghttp2")) {
      // For HTTP/2 pseudo header, use the HTTP/2 semantics for checking validity
      return nghttp2_check_header_name(reinterpret_cast<const uint8_t*>(header_key.data()),
                                       header_key.size()) != 0;
    }
#endif
    header_key.remove_prefix(1);
    if (header_key.empty()) {
      return false;
    }
  }
  // For all other header use HTTP/1 semantics. The only difference from HTTP/2 is that
  // uppercase characters are allowed. This allows HTTP filters to add header with mixed
  // case names. The HTTP/1 codec will send as is, as uppercase characters are allowed.
  // However the HTTP/2 codec will NOT convert these to lowercase when serializing the
  // header map, thus producing an invalid request.
  // TODO(yanavlasov): make validation in HTTP/2 case stricter.
  bool is_valid = true;
  for (auto iter = header_key.begin(); iter != header_key.end() && is_valid; ++iter) {
    is_valid &= testCharInTable(kGenericHeaderNameCharTable, *iter);
  }
  return is_valid;
}

bool HeaderUtility::headerNameContainsUnderscore(const absl::string_view header_name) {
  return header_name.find('_') != absl::string_view::npos;
}

namespace {
// This function validates the authority header for both HTTP/1 and HTTP/2.
// Note the HTTP/1 spec allows "user-info@host:port" for the authority, whereas
// the HTTP/2 spec only allows "host:port". Thus, this function permits all the
// HTTP/2 valid characters (similar to oghttp2's implementation) and the "@" character.
// Once UHV is used, this function should be removed, and the HTTP/1 and HTTP/2
// authority validations should be different.
bool checkAuthorityH1H2(const absl::string_view header_value) {
  static constexpr char ValidAuthorityChars[] = {
      0 /* NUL  */, 0 /* SOH  */, 0 /* STX  */, 0 /* ETX  */,
      0 /* EOT  */, 0 /* ENQ  */, 0 /* ACK  */, 0 /* BEL  */,
      0 /* BS   */, 0 /* HT   */, 0 /* LF   */, 0 /* VT   */,
      0 /* FF   */, 0 /* CR   */, 0 /* SO   */, 0 /* SI   */,
      0 /* DLE  */, 0 /* DC1  */, 0 /* DC2  */, 0 /* DC3  */,
      0 /* DC4  */, 0 /* NAK  */, 0 /* SYN  */, 0 /* ETB  */,
      0 /* CAN  */, 0 /* EM   */, 0 /* SUB  */, 0 /* ESC  */,
      0 /* FS   */, 0 /* GS   */, 0 /* RS   */, 0 /* US   */,
      0 /* SPC  */, 1 /* !    */, 0 /* "    */, 0 /* #    */,
      1 /* $    */, 1 /* %    */, 1 /* &    */, 1 /* '    */,
      1 /* (    */, 1 /* )    */, 1 /* *    */, 1 /* +    */,
      1 /* ,    */, 1 /* -    */, 1 /* . */,    0 /* /    */,
      1 /* 0    */, 1 /* 1    */, 1 /* 2    */, 1 /* 3    */,
      1 /* 4    */, 1 /* 5    */, 1 /* 6    */, 1 /* 7    */,
      1 /* 8    */, 1 /* 9    */, 1 /* :    */, 1 /* ;    */,
      0 /* <    */, 1 /* =    */, 0 /* >    */, 0 /* ?    */,
      1 /* @    */, 1 /* A    */, 1 /* B    */, 1 /* C    */,
      1 /* D    */, 1 /* E    */, 1 /* F    */, 1 /* G    */,
      1 /* H    */, 1 /* I    */, 1 /* J    */, 1 /* K    */,
      1 /* L    */, 1 /* M    */, 1 /* N    */, 1 /* O    */,
      1 /* P    */, 1 /* Q    */, 1 /* R    */, 1 /* S    */,
      1 /* T    */, 1 /* U    */, 1 /* V    */, 1 /* W    */,
      1 /* X    */, 1 /* Y    */, 1 /* Z    */, 1 /* [    */,
      0 /* \    */, 1 /* ]    */, 0 /* ^    */, 1 /* _    */,
      0 /* `    */, 1 /* a    */, 1 /* b    */, 1 /* c    */,
      1 /* d    */, 1 /* e    */, 1 /* f    */, 1 /* g    */,
      1 /* h    */, 1 /* i    */, 1 /* j    */, 1 /* k    */,
      1 /* l    */, 1 /* m    */, 1 /* n    */, 1 /* o    */,
      1 /* p    */, 1 /* q    */, 1 /* r    */, 1 /* s    */,
      1 /* t    */, 1 /* u    */, 1 /* v    */, 1 /* w    */,
      1 /* x    */, 1 /* y    */, 1 /* z    */, 0 /* {    */,
      0 /* |    */, 0 /* }    */, 1 /* ~    */, 0 /* DEL  */,
      0 /* 0x80 */, 0 /* 0x81 */, 0 /* 0x82 */, 0 /* 0x83 */,
      0 /* 0x84 */, 0 /* 0x85 */, 0 /* 0x86 */, 0 /* 0x87 */,
      0 /* 0x88 */, 0 /* 0x89 */, 0 /* 0x8a */, 0 /* 0x8b */,
      0 /* 0x8c */, 0 /* 0x8d */, 0 /* 0x8e */, 0 /* 0x8f */,
      0 /* 0x90 */, 0 /* 0x91 */, 0 /* 0x92 */, 0 /* 0x93 */,
      0 /* 0x94 */, 0 /* 0x95 */, 0 /* 0x96 */, 0 /* 0x97 */,
      0 /* 0x98 */, 0 /* 0x99 */, 0 /* 0x9a */, 0 /* 0x9b */,
      0 /* 0x9c */, 0 /* 0x9d */, 0 /* 0x9e */, 0 /* 0x9f */,
      0 /* 0xa0 */, 0 /* 0xa1 */, 0 /* 0xa2 */, 0 /* 0xa3 */,
      0 /* 0xa4 */, 0 /* 0xa5 */, 0 /* 0xa6 */, 0 /* 0xa7 */,
      0 /* 0xa8 */, 0 /* 0xa9 */, 0 /* 0xaa */, 0 /* 0xab */,
      0 /* 0xac */, 0 /* 0xad */, 0 /* 0xae */, 0 /* 0xaf */,
      0 /* 0xb0 */, 0 /* 0xb1 */, 0 /* 0xb2 */, 0 /* 0xb3 */,
      0 /* 0xb4 */, 0 /* 0xb5 */, 0 /* 0xb6 */, 0 /* 0xb7 */,
      0 /* 0xb8 */, 0 /* 0xb9 */, 0 /* 0xba */, 0 /* 0xbb */,
      0 /* 0xbc */, 0 /* 0xbd */, 0 /* 0xbe */, 0 /* 0xbf */,
      0 /* 0xc0 */, 0 /* 0xc1 */, 0 /* 0xc2 */, 0 /* 0xc3 */,
      0 /* 0xc4 */, 0 /* 0xc5 */, 0 /* 0xc6 */, 0 /* 0xc7 */,
      0 /* 0xc8 */, 0 /* 0xc9 */, 0 /* 0xca */, 0 /* 0xcb */,
      0 /* 0xcc */, 0 /* 0xcd */, 0 /* 0xce */, 0 /* 0xcf */,
      0 /* 0xd0 */, 0 /* 0xd1 */, 0 /* 0xd2 */, 0 /* 0xd3 */,
      0 /* 0xd4 */, 0 /* 0xd5 */, 0 /* 0xd6 */, 0 /* 0xd7 */,
      0 /* 0xd8 */, 0 /* 0xd9 */, 0 /* 0xda */, 0 /* 0xdb */,
      0 /* 0xdc */, 0 /* 0xdd */, 0 /* 0xde */, 0 /* 0xdf */,
      0 /* 0xe0 */, 0 /* 0xe1 */, 0 /* 0xe2 */, 0 /* 0xe3 */,
      0 /* 0xe4 */, 0 /* 0xe5 */, 0 /* 0xe6 */, 0 /* 0xe7 */,
      0 /* 0xe8 */, 0 /* 0xe9 */, 0 /* 0xea */, 0 /* 0xeb */,
      0 /* 0xec */, 0 /* 0xed */, 0 /* 0xee */, 0 /* 0xef */,
      0 /* 0xf0 */, 0 /* 0xf1 */, 0 /* 0xf2 */, 0 /* 0xf3 */,
      0 /* 0xf4 */, 0 /* 0xf5 */, 0 /* 0xf6 */, 0 /* 0xf7 */,
      0 /* 0xf8 */, 0 /* 0xf9 */, 0 /* 0xfa */, 0 /* 0xfb */,
      0 /* 0xfc */, 0 /* 0xfd */, 0 /* 0xfe */, 0 /* 0xff */
  };

  for (const uint8_t c : header_value) {
    if (!ValidAuthorityChars[c]) {
      return false;
    }
  }
  return true;
}
} // namespace

bool HeaderUtility::authorityIsValid(const absl::string_view header_value) {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.internal_authority_header_validator")) {
    return checkAuthorityH1H2(header_value);
  }
  return http2::adapter::HeaderValidator::IsValidAuthority(header_value);
}

bool HeaderUtility::isSpecial1xx(const ResponseHeaderMap& response_headers) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.proxy_104") &&
      response_headers.Status()->value() == "104") {
    return true;
  }
  return response_headers.Status()->value() == "100" ||
         response_headers.Status()->value() == "102" || response_headers.Status()->value() == "103";
}

bool HeaderUtility::isConnect(const RequestHeaderMap& headers) {
  return headers.Method() && headers.Method()->value() == Http::Headers::get().MethodValues.Connect;
}

bool HeaderUtility::isConnectUdpRequest(const RequestHeaderMap& headers) {
  return headers.Upgrade() && absl::EqualsIgnoreCase(headers.getUpgradeValue(),
                                                     Http::Headers::get().UpgradeValues.ConnectUdp);
}

bool HeaderUtility::isConnectUdpResponse(const ResponseHeaderMap& headers) {
  // In connect-udp case, Envoy will transform the H2 headers to H1 upgrade headers.
  // A valid response should have SwitchingProtocol status and connect-udp upgrade.
  return headers.Upgrade() && Utility::getResponseStatus(headers) == 101 &&
         absl::EqualsIgnoreCase(headers.getUpgradeValue(),
                                Http::Headers::get().UpgradeValues.ConnectUdp);
}

bool HeaderUtility::isConnectResponse(const RequestHeaderMap* request_headers,
                                      const ResponseHeaderMap& response_headers) {
  return request_headers && isConnect(*request_headers) &&
         static_cast<Http::Code>(Http::Utility::getResponseStatus(response_headers)) ==
             Http::Code::OK;
}

bool HeaderUtility::rewriteAuthorityForConnectUdp(RequestHeaderMap& headers) {
  // Per RFC 9298, the URI template must only contain ASCII characters in the range 0x21-0x7E.
  absl::string_view path = headers.getPathValue();
  for (char c : path) {
    unsigned char ascii_code = static_cast<unsigned char>(c);
    if (ascii_code < 0x21 || ascii_code > 0x7e) {
      ENVOY_LOG_MISC(warn, "CONNECT-UDP request with a bad character in the path {}", path);
      return false;
    }
  }

  // Extract target host and port from path using default template.
  if (!absl::StartsWith(path, "/.well-known/masque/udp/")) {
    ENVOY_LOG_MISC(warn, "CONNECT-UDP request path is not a well-known URI: {}", path);
    return false;
  }

  std::vector<absl::string_view> path_split = absl::StrSplit(path, '/');
  if (path_split.size() != 7 || path_split[4].empty() || path_split[5].empty() ||
      !path_split[6].empty()) {
    ENVOY_LOG_MISC(warn, "CONNECT-UDP request with a malformed URI template in the path {}", path);
    return false;
  }

  // Utility::PercentEncoding::decode never returns an empty string if the input argument is not
  // empty.
  std::string target_host = Utility::PercentEncoding::decode(path_split[4]);
  // Per RFC 9298, IPv6 Zone ID is not supported.
  if (target_host.find('%') != std::string::npos) {
    ENVOY_LOG_MISC(warn, "CONNECT-UDP request with a non-escpaed char (%) in the path {}", path);
    return false;
  }
  std::string target_port = Utility::PercentEncoding::decode(path_split[5]);

  // If the host is an IPv6 address, surround the address with square brackets.
  in6_addr sin6_addr;
  bool is_ipv6 = (inet_pton(AF_INET6, target_host.c_str(), &sin6_addr) == 1);
  std::string new_host =
      absl::StrCat((is_ipv6 ? absl::StrCat("[", target_host, "]") : target_host), ":", target_port);
  headers.setHost(new_host);

  return true;
}

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
bool HeaderUtility::isCapsuleProtocol(const RequestOrResponseHeaderMap& headers) {
  Http::HeaderMap::GetResult capsule_protocol =
      headers.get(Envoy::Http::LowerCaseString("Capsule-Protocol"));
  // When there are multiple Capsule-Protocol header entries, it returns false. RFC 9297 specifies
  // that non-boolean value types must be ignored. If there are multiple header entries, the value
  // type becomes a List so the header field must be ignored.
  if (capsule_protocol.size() != 1) {
    return false;
  }
  // Parses the header value and extracts the boolean value ignoring parameters.
  absl::optional<quiche::structured_headers::ParameterizedItem> header_item =
      quiche::structured_headers::ParseItem(capsule_protocol[0]->value().getStringView());
  return header_item && header_item->item.is_boolean() && header_item->item.GetBoolean();
}
#endif

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

bool HeaderUtility::hostHasPort(absl::string_view original_host) {
  const absl::string_view::size_type port_start = getPortStart(original_host);
  const absl::string_view port_str = original_host.substr(port_start + 1);
  if (port_start == absl::string_view::npos) {
    return false;
  }
  uint32_t port = 0;
  if (!absl::SimpleAtoi(port_str, &port)) {
    return false;
  }
  return true;
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

void HeaderUtility::stripPortFromHost(std::string& host) {
  const absl::string_view::size_type port_start = getPortStart(host);
  if (port_start == absl::string_view::npos) {
    return;
  }
  host = host.substr(0, port_start);
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
  const absl::optional<uint64_t> status = Utility::getResponseStatusOrNullopt(headers);
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
    HeaderValidatorStats& stats) {
  if (headers_with_underscores_action == envoy::config::core::v3::HttpProtocolOptions::ALLOW ||
      !HeaderUtility::headerNameContainsUnderscore(header_name)) {
    return HeaderValidationResult::ACCEPT;
  }
  if (headers_with_underscores_action ==
      envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER) {
    ENVOY_LOG_MISC(debug, "Dropping header with invalid characters in its name: {}", header_name);
    stats.incDroppedHeadersWithUnderscores();
    return HeaderValidationResult::DROP;
  }
  ENVOY_LOG_MISC(debug, "Rejecting request due to header name with underscores: {}", header_name);
  stats.incRequestsRejectedWithUnderscoresInHeaders();
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

bool HeaderUtility::isStandardConnectRequest(const Http::RequestHeaderMap& headers) {
  return headers.getMethodValue() == Http::Headers::get().MethodValues.Connect &&
         headers.getProtocolValue().empty();
}

bool HeaderUtility::isExtendedH2ConnectRequest(const Http::RequestHeaderMap& headers) {
  return headers.getMethodValue() == Http::Headers::get().MethodValues.Connect &&
         !headers.getProtocolValue().empty();
}

bool HeaderUtility::isPseudoHeader(absl::string_view header_name) {
  return !header_name.empty() && header_name[0] == ':';
}

} // namespace Http
} // namespace Envoy
