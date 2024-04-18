#pragma once

#include <cstdint>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Utf8 {

// Constants used for decoding UTF-8 sequences. These are primarily needed
// by decodeUtf8, but are also useful for writing tests that cover all
// possible utf-8 encodings.
static constexpr uint32_t Mask1Byte = 0b10000000;
static constexpr uint32_t Mask2Byte = 0b11100000;
static constexpr uint32_t Mask3Byte = 0b11110000;
static constexpr uint32_t Mask4Byte = 0b11111000;

static constexpr uint32_t Pattern1Byte = 0b00000000;
static constexpr uint32_t Pattern2Byte = 0b11000000;
static constexpr uint32_t Pattern3Byte = 0b11100000;
static constexpr uint32_t Pattern4Byte = 0b11110000;

static constexpr uint32_t ContinueMask = 0b11000000;
static constexpr uint32_t ContinuePattern = 0b10000000;

static constexpr uint32_t Shift = 6;

using UnicodeSizePair = std::pair<uint32_t, uint32_t>;

/**
 * Decodes a single Utf8-encoded code-point from the string,
 * @param str A possibly Utf-8 encoded string.
 * @return the pair containing the first Unicode symbol from str, and the number of bytes
 *         consumed from str. If str does not start with a valid UTF-8 code sequence,
 *         then zero is returned for the size (UnicodeSizePair.second) and the returned
 *         Unicode value should be ignored.
 */
UnicodeSizePair decode(const uint8_t* bytes, uint32_t size);
inline UnicodeSizePair decode(absl::string_view str) {
  return decode(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

} // namespace Utf8
} // namespace Json
} // namespace Envoy
