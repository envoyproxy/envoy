#pragma once

#include <cstdint>

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

constexpr bool testChar(const uint32_t table[8], char c) {
  uint8_t uc = static_cast<uint8_t>(c);
  return (table[uc >> 5] & (0x80000000 >> (uc & 0x1f))) != 0;
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
constexpr uint32_t kGenericHeaderNameCharTable[] = {
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
constexpr uint32_t kGenericHeaderValueCharTable[] = {
    // control characters
    0b00000000010000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b11111111111111111111111111111111,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b11111111111111111111111111111111,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b11111111111111111111111111111110,
    // extended ascii
    0b11111111111111111111111111111111,
    0b11111111111111111111111111111111,
    0b11111111111111111111111111111111,
    0b11111111111111111111111111111111,
};

// :method header character table.
// From RFC 9110: https://www.rfc-editor.org/rfc/rfc9110.html#section-9.1
//
// SPELLCHECKER(off)
// method = token
// token  = 1*tchar
// tchar  = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "."
//        /  "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
// SPELLCHECKER(on)
constexpr uint32_t kMethodHeaderCharTable[] = {
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
constexpr uint32_t kPathHeaderCharTable[] = {
    // control characters
    0b00000000000000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b01001111111111111111111111110100,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b11111111111111111111111111100001,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b01111111111111111111111111100010,
    // extended ascii
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
};

// Unreserved characters.
// From RFC 3986: https://datatracker.ietf.org/doc/html/rfc3986#section-2.3
//
// unreserved  = ALPHA / DIGIT / "-" / "." / "_" / "~"
constexpr uint32_t kUnreservedCharTable[] = {
    // control characters
    0b00000000000000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b00000000000001101111111111000000,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b01111111111111111111111111100001,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b01111111111111111111111111100010,
    // extended ascii
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
};

// Transfer-Encoding HTTP/1.1 header character table.
// From RFC 9110: https://www.rfc-editor.org/rfc/rfc9110.html#section-10.1.4
//
// SPELLCHECKER(off)
// Transfer-Encoding   = #transfer-coding
// transfer-coding     = token *( OWS ";" OWS transfer-parameter )
// transfer-parameter  = token BWS "=" BWS ( token / quoted-string )
// SPELLCHECKER(on)
constexpr uint32_t kTransferEncodingHeaderCharTable[] = {
    // control characters
    0b00000000010000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b11111111001111101111111111010100,
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

// An IPv6 address, excluding the surrounding "[" and "]" characters. This is based on RFC 3986,
// https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2, that only allows hex digits and the
// ":" separator.
constexpr uint32_t kHostIPv6AddressCharTable[] = {
    // control characters
    0b00000000000000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b00000000000000001111111111100000,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b01111110000000000000000000000000,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b01111110000000000000000000000000,
    // extended ascii
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
};

// A host reg-name character table, which covers both IPv4 addresses and hostnames.
// From RFC 3986: https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2
//
// SPELLCHECKER(off)
// reg-name    = *( unreserved / pct-encoded / sub-delims )
// SPELLCHECKER(on)
constexpr uint32_t kHostRegNameCharTable[] = {
    // control characters
    0b00000000000000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b01001111111111101111111111010100,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b01111111111111111111111111100001,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b01111111111111111111111111100010,
    // extended ascii
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
};

// A URI query and fragment character table. From RFC 3986:
// https://datatracker.ietf.org/doc/html/rfc3986#section-3.4
//
// SPELLCHECKER(off)
// query       = *( pchar / "/" / "?" )
// fragment    = *( pchar / "/" / "?" )
// SPELLCHECKER(on)
constexpr uint32_t kUriQueryAndFragmentCharTable[] = {
    // control characters
    0b00000000000000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b01001111111111111111111111110101,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b11111111111111111111111111100001,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b01111111111111111111111111100010,
    // extended ascii
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
