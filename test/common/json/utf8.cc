#include "test/common/json/utf8.h"

namespace Envoy {
namespace Json {
namespace Utf8 {

UnicodeSizePair decode(const uint8_t* bytes, uint32_t size) {
  uint32_t unicode = 0;
  uint32_t consumed = 0;

  // See table in https://en.wikipedia.org/wiki/UTF-8, "Encoding" section.
  //
  // See also https://en.cppreference.com/w/cpp/locale/codecvt_utf8 which is
  // marked as deprecated. There is also support in Windows libraries and Boost,
  // which can be discovered on StackOverflow. I could not find a usable OSS
  // implementation. However it's easily derived from the spec on Wikipedia.
  //
  // Note that the code below could be optimized a bit, e.g. by factoring out
  // repeated lookups of the same index in the bytes array and using SSE
  // instructions for the multi-word bit hacking.
  //
  // See also http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ which might be a lot
  // faster, though less readable. As coded, though, it looks like it would read
  // past the end of the input if the input is malformed.
  if (size >= 1 && (bytes[0] & Utf8::Mask1Byte) == Utf8::Pattern1Byte) {
    unicode = bytes[0] & ~Utf8::Mask1Byte;
    consumed = 1;
  } else if (size >= 2 && (bytes[0] & Utf8::Mask2Byte) == Utf8::Pattern2Byte &&
             (bytes[1] & Utf8::ContinueMask) == Utf8::ContinuePattern) {
    unicode = bytes[0] & ~Utf8::Mask2Byte;
    unicode = (unicode << Utf8::Shift) | (bytes[1] & ~Utf8::ContinueMask);
    if (unicode < 0x80) {
      return {0, 0};
    }
    consumed = 2;
  } else if (size >= 3 && (bytes[0] & Utf8::Mask3Byte) == Utf8::Pattern3Byte &&
             (bytes[1] & Utf8::ContinueMask) == Utf8::ContinuePattern &&
             (bytes[2] & Utf8::ContinueMask) == Utf8::ContinuePattern) {
    unicode = bytes[0] & ~Utf8::Mask3Byte;
    unicode = (unicode << Utf8::Shift) | (bytes[1] & ~Utf8::ContinueMask);
    unicode = (unicode << Utf8::Shift) | (bytes[2] & ~Utf8::ContinueMask);
    if (unicode < 0x800) { // 3-byte starts at 0x800
      return {0, 0};
    }
    consumed = 3;
  } else if (size >= 4 && (bytes[0] & Utf8::Mask4Byte) == Utf8::Pattern4Byte &&
             (bytes[1] & Utf8::ContinueMask) == Utf8::ContinuePattern &&
             (bytes[2] & Utf8::ContinueMask) == Utf8::ContinuePattern &&
             (bytes[3] & Utf8::ContinueMask) == Utf8::ContinuePattern) {
    unicode = bytes[0] & ~Utf8::Mask4Byte;
    unicode = (unicode << Utf8::Shift) | (bytes[1] & ~Utf8::ContinueMask);
    unicode = (unicode << Utf8::Shift) | (bytes[2] & ~Utf8::ContinueMask);
    unicode = (unicode << Utf8::Shift) | (bytes[3] & ~Utf8::ContinueMask);

    // 4-byte starts at 0x10000
    //
    // Note from https://en.wikipedia.org/wiki/UTF-8:
    // The earlier RFC2279 allowed UTF-8 encoding through code point 0x7FFFFFF.
    // But the current RFC3629 section 3 limits UTF-8 encoding through code
    // point 0x10FFFF, to match the limits of UTF-16.
    if (unicode < 0x10000 || unicode > 0x10ffff) {
      return {0, 0};
    }
    consumed = 4;
  }
  return {unicode, consumed};
}

} // namespace Utf8
} // namespace Json
} // namespace Envoy
