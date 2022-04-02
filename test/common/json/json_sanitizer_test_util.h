#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace TestUtil {

static constexpr uint32_t Utf8_1ByteMask = 0b10000000;
static constexpr uint32_t Utf8_2ByteMask = 0b11100000;
static constexpr uint32_t Utf8_3ByteMask = 0b11110000;
static constexpr uint32_t Utf8_4ByteMask = 0b11111000;

static constexpr uint32_t Utf8_1BytePattern = 0b00000000;
static constexpr uint32_t Utf8_2BytePattern = 0b11000000;
static constexpr uint32_t Utf8_3BytePattern = 0b11100000;
static constexpr uint32_t Utf8_4BytePattern = 0b11110000;

static constexpr uint32_t Utf8_ContinueMask = 0b11000000;
static constexpr uint32_t Utf8_ContinuePattern = 0b10000000;

static constexpr uint32_t Utf8_Shift = 6;

/**
 * Strips double-quotes on first and last characters of str.
 *
 * @param str The string to strip double-quotes from.
 * @return The string without its surrounding double-quotes.
 */
absl::string_view stripDoubleQuotes(absl::string_view str);

/**
 * Determines whether the input string can be serialized by protobufs. This is
 * used for testing, to avoid trying to do differentials against Protobuf json
 * sanitization, which produces noisy error messages and empty strings when
 * presented with some utf8 sequences that are valid according to spec.
 *
 * @param in the string to validate as utf-8.
 */
bool isProtoSerializableUtf8(absl::string_view in);

bool utf8Equivalent(absl::string_view a, absl::string_view b, std::string& errmsg);
#define EXPECT_UTF8_EQ(a, b, context) {            \
  std::string errmsg; \
  EXPECT_TRUE(TestUtil::utf8Equivalent(a, b, errmsg)) << context << "\n" << errmsg; \
}

/** The Unicode code-point and the number of utf8-bytes consumed */
using UnicodeSizePair = std::pair<uint32_t, uint32_t>;

/**
 * Decodes a byte-stream of UTF8, returning the resulting unicode and the
 * number of bytes consumed as a pair.
 *
 * @param bytes The data with utf8 bytes.
 * @param size The number of bytes available in data
 * @return UnicodeSizePair(unicode, consumed) -- if the decode fails consumed will be 0.
 */
UnicodeSizePair decodeUtf8(const uint8_t* bytes, uint32_t size);
inline UnicodeSizePair decodeUtf8(absl::string_view str) {
  return decodeUtf8(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

} // namespace TestUtil
} // namespace Json
} // namespace Envoy
