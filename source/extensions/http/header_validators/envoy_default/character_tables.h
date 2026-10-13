#pragma once

#include <cstdint>

#include "source/common/http/character_set_validation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

// Header value character table.
// From RFC 9110, https://www.rfc-editor.org/rfc/rfc9110.html#section-5.5:
//
// SPELLCHECKER(off)
// header-field   = field-name ":" OWS field-value OWS
// field-value    = *field-content
// field-content  = field-vchar
//                  [ 1*( SP / HTAB / field-vchar ) field-vchar ]
// field-vchar    = VCHAR / obs-text
// obs-text       = %x80-FF
//
// VCHAR          =  %x21-7E
//                   ; visible (printing) characters
// SPELLCHECKER(on)
inline constexpr ::Envoy::Http::CharTable kGenericHeaderValueCharTable =
    ::Envoy::Http::CharTables::kPrintable | ::Envoy::Http::CharTables::kExtendedAscii |
    ::Envoy::Http::CharTable::fromChars("\t ");

// :method header character table.
// From RFC 9110: https://www.rfc-editor.org/rfc/rfc9110.html#section-9.1
//
// SPELLCHECKER(off)
// method = token
// token  = 1*tchar
// tchar  = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "."
//        /  "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
// SPELLCHECKER(on)
inline constexpr ::Envoy::Http::CharTable kMethodHeaderCharTable =
    ::Envoy::Http::CharTables::kAlphanumeric |
    ::Envoy::Http::CharTable::fromChars("!#$%&'*+-.^_`|~");

// :path header character table.
// From RFC 3986: https://datatracker.ietf.org/doc/html/rfc3986#section-3.3
//
// SPELLCHECKER(off)
// path          = path-abempty    ; begins with "/" or is empty
//               / path-absolute   ; begins with "/" but not "//"
//               / path-noscheme   ; begins with a non-colon segment
//               / path-rootless   ; begins with a segment
//               / path-empty      ; zero characters
//
// path-abempty  = *( "/" segment )
// path-absolute = "/" [ segment-nz *( "/" segment ) ]
// path-noscheme = segment-nz-nc *( "/" segment )
// path-rootless = segment-nz *( "/" segment )
// path-empty    = 0<pchar>
//
// segment       = *pchar
// segment-nz    = 1*pchar
// segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
//               ; non-zero-length segment without any colon ":"
//
// pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
// SPELLCHECKER(on)
inline constexpr ::Envoy::Http::CharTable kPathHeaderCharTable =
    ::Envoy::Http::CharTables::kAlphanumeric |
    ::Envoy::Http::CharTable::fromChars("!$%&'()*+,-./:;=@_~");

// Unreserved characters.
// From RFC 3986: https://datatracker.ietf.org/doc/html/rfc3986#section-2.3
//
// unreserved  = ALPHA / DIGIT / "-" / "." / "_" / "~"
inline constexpr ::Envoy::Http::CharTable kUnreservedCharTable =
    ::Envoy::Http::CharTables::kAlphanumeric | ::Envoy::Http::CharTable::fromChars("-._~");

// Transfer-Encoding HTTP/1.1 header character table.
// From RFC 9110: https://www.rfc-editor.org/rfc/rfc9110.html#section-10.1.4
//
// SPELLCHECKER(off)
// Transfer-Encoding   = #transfer-coding
// transfer-coding     = token *( OWS ";" OWS transfer-parameter )
// transfer-parameter  = token BWS "=" BWS ( token / quoted-string )
// SPELLCHECKER(on)
inline constexpr ::Envoy::Http::CharTable kTransferEncodingHeaderCharTable =
    ::Envoy::Http::CharTables::kAlphanumeric |
    ::Envoy::Http::CharTable::fromChars("\t !\"#$%&'*+,-.;=^_`|~");

// An IPv6 address, excluding the surrounding "[" and "]" characters. This is based on RFC 3986,
// https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2, that only allows hex digits and the
// ":" separator.
inline constexpr ::Envoy::Http::CharTable kHostIPv6AddressCharTable =
    ::Envoy::Http::CharTable::fromChars("0123456789:ABCDEFabcdef");

// A host reg-name character table, which covers both IPv4 addresses and hostnames.
// From RFC 3986: https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2
//
// SPELLCHECKER(off)
// reg-name    = *( unreserved / pct-encoded / sub-delims )
// SPELLCHECKER(on)
inline constexpr ::Envoy::Http::CharTable kHostRegNameCharTable =
    ::Envoy::Http::CharTables::kAlphanumeric |
    ::Envoy::Http::CharTable::fromChars("!$%&'()*+,-.;=_~");

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
