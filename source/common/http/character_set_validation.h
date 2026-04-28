#pragma once

#include <array>
#include <cstdint>

#include "absl/strings/string_view.h"

// A set of tables for validating that a character is in a specific
// character set. Used to validate RFC compliance for various HTTP protocol elements.

namespace Envoy {
namespace Http {

struct CharTable {
  const std::array<uint32_t, 8> table_;

  static inline constexpr uint32_t row(char c) { return static_cast<uint8_t>(c) >> 5; }
  static inline constexpr uint32_t mask(char c) {
    return 0x80000000 >> (static_cast<uint8_t>(c) & 0x1f);
  }
  inline constexpr bool hasChar(char c) const { return (table_[row(c)] & mask(c)) != 0; }
  inline static constexpr void set(std::array<uint32_t, 8>& table, char c) {
    table[row(c)] |= mask(c);
  }
  static inline constexpr CharTable fromChars(absl::string_view chars) {
    std::array<uint32_t, 8> table{};
    for (char c : chars) {
      set(table, c);
    }
    return {table};
  }
  constexpr CharTable operator|(const CharTable& o) const {
    std::array<uint32_t, 8> table;
    for (int i = 0; i < 8; i++) {
      table[i] = table_[i] | o.table_[i];
    }
    return {table};
  }
  constexpr CharTable operator&(const CharTable& o) const {
    std::array<uint32_t, 8> table;
    for (int i = 0; i < 8; i++) {
      table[i] = table_[i] & o.table_[i];
    }
    return {table};
  }
  constexpr CharTable operator~() const {
    std::array<uint32_t, 8> table;
    for (int i = 0; i < 8; i++) {
      table[i] = ~table_[i];
    }
    return {table};
  }
};

namespace CharTables {
// Bits 65 (A) to 90 (Z)
static inline constexpr CharTable kUppercase{
    {0, 0, 0b01111111111111111111111111100000, 0, 0, 0, 0, 0}};
// Bits 97 (a) to 122 (z)
static inline constexpr CharTable kLowercase{
    {0, 0, 0, 0b01111111111111111111111111100000, 0, 0, 0, 0}};
// Bits 33 (!) to 127 (~).
static inline constexpr CharTable kPrintable{{0, 0x7fffffff, 0xffffffff, 0xfffffffe, 0, 0, 0, 0}};
// Bits 129 to 255.
static inline constexpr CharTable kExtendedAscii{
    {0, 0, 0, 0, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff}};
// Bits 48 ('0') to 57 ('9')
static inline constexpr CharTable kDigits{
    {0, 0b00000000000000001111111111000000, 0, 0, 0, 0, 0, 0}};
static inline constexpr CharTable kAlphanumeric = kUppercase | kLowercase | kDigits;
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
inline constexpr CharTable kGenericHeaderName =
    kAlphanumeric | CharTable::fromChars("!#$%&'*+-.^_`|~");

// A URI query and fragment character table. From RFC 3986:
// https://datatracker.ietf.org/doc/html/rfc3986#section-3.4
//
// SPELLCHECKER(off)
// query       = *( pchar / "/" / "?" )
// fragment    = *( pchar / "/" / "?" )
//
// pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
// unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
// pct-encoded   = "%" HEXDIG HEXDIG
// sub-delims    = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
// SPELLCHECKER(on)
inline constexpr CharTable kUriQueryAndFragment =
    kAlphanumeric | CharTable::fromChars("/?"
                                         ":@"
                                         "-._~"
                                         "%"
                                         "!$&'()*+,;=");
} // namespace CharTables

} // namespace Http
} // namespace Envoy
