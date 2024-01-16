#pragma once

#include <array>
#include <cstdint>

// A set of tables for validating that a character is in a specific
// character set. Used to validate RFC compliance for various HTTP protocol elements.

namespace Envoy {
namespace Http {

inline constexpr bool testCharInTable(const std::array<uint32_t, 8>& table, char c) {
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
inline constexpr std::array<uint32_t, 8> kGenericHeaderNameCharTable = {
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

// A URI query and fragment character table. From RFC 3986:
// https://datatracker.ietf.org/doc/html/rfc3986#section-3.4
//
// SPELLCHECKER(off)
// query       = *( pchar / "/" / "?" )
// fragment    = *( pchar / "/" / "?" )
// SPELLCHECKER(on)
inline constexpr std::array<uint32_t, 8> kUriQueryAndFragmentCharTable = {
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

} // namespace Http
} // namespace Envoy
