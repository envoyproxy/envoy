#include "source/common/http/header_validation.h"

#include "source/common/common/json_escape_string.h"
#include "source/common/runtime/runtime_features.h"

#ifdef ENVOY_NGHTTP2
#include "nghttp2/nghttp2.h"
#endif

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "quiche/common/structured_headers.h"
#endif
#include "quiche/http2/adapter/header_validator.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {
namespace HeaderValidation {

namespace {
// This function validates the authority header for both HTTP/1 and HTTP/2.
// Note the HTTP/1 spec allows "user-info@host:port" for the authority, whereas
// the HTTP/2 spec only allows "host:port". Thus, this function permits all the
// HTTP/2 valid characters (similar to oghttp2's implementation) and the "@" character.
// Once UHV is used, this function should be removed, and the HTTP/1 and HTTP/2
// authority validations should be different.
bool check_authority_h1_h2(const absl::string_view header_value) {
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

struct SharedResponseCodeDetailsValues {
  const absl::string_view InvalidAuthority = "http.invalid_authority";
  const absl::string_view ConnectUnsupported = "http.connect_not_supported";
  const absl::string_view InvalidMethod = "http.invalid_method";
  const absl::string_view InvalidPath = "http.invalid_path";
  const absl::string_view InvalidScheme = "http.invalid_scheme";
};

using SharedResponseCodeDetails = ConstSingleton<SharedResponseCodeDetailsValues>;

} // namespace

absl::optional<std::reference_wrapper<const absl::string_view>>
requestHeadersValid(const RequestHeaderMap& headers) {
  // Make sure the host is valid.
  if (headers.Host() && !authorityIsValid(headers.Host()->value().getStringView())) {
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

bool authorityIsValid(const absl::string_view header_value) {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.internal_authority_header_validator")) {
    return check_authority_h1_h2(header_value);
  }
  return http2::adapter::HeaderValidator::IsValidAuthority(header_value);
}

bool headerNameIsValid(absl::string_view header_key) {
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

bool headerValueIsValid(const absl::string_view header_value) {
  return http2::adapter::HeaderValidator::IsValidHeaderValue(header_value,
                                                             http2::adapter::ObsTextOption::kAllow);
}

Http::Status checkValidRequestHeaders(const Http::RequestHeaderMap& headers) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.validate_upstream_headers")) {
    return Http::okStatus();
  }

  const HeaderEntry* invalid_entry = nullptr;
  bool invalid_key = false;
  headers.iterate([&invalid_entry, &invalid_key](const HeaderEntry& header) -> HeaderMap::Iterate {
    if (!headerNameIsValid(header.key().getStringView())) {
      invalid_entry = &header;
      invalid_key = true;
      return HeaderMap::Iterate::Break;
    }

    if (!headerValueIsValid(header.value().getStringView())) {
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

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
bool isCapsuleProtocol(const RequestOrResponseHeaderMap& headers) {
  static const LowerCaseString capsule_protocol_key("capsule-protocol");
  Http::HeaderMap::GetResult capsule_protocol = headers.get(capsule_protocol_key);
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

} // namespace HeaderValidation
} // namespace Http
} // namespace Envoy
