#pragma once

#include <charconv>
#include <cstddef>

#include "envoy/buffer/buffer.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Buffer {

class Util {
public:
  /**
   * Serializes double to a buffer with high precision and high performance.
   *
   * This helper function is defined on Buffer rather than working with
   * intermediate string constructs because, depending on the platform, a
   * different sort of intermediate char buffer is chosen for maximum
   * performance. It's fastest to then directly append the serialized
   * char-buffer into the Buffer::Instance, without defining the intermediate
   * char-buffer as part of the API.
   *
   * @param number the number to convert.
   * @param buffer the buffer in which to write the double.
   */
  template <class Output> static void serializeDouble(double number, Output& buffer) {
    // Converting a double to a string: who would think it would be so complex?
    // It's easy if you don't care about speed or accuracy :). Here we are measuring
    // the speed with test/server/admin/stats_handler_speed_test
    // --benchmark_filter=BM_HistogramsJson Here are some options:
    //   * absl::StrCat(number) -- fast (19ms on speed test) but loses precision (drops decimals).
    //   * absl::StrFormat("%.15g") -- works great but a bit slow (24ms on speed test)
    //   * `snprintf`(buf, sizeof(buf), "%.15g", ...) -- works but slow as molasses: 30ms.
    //   * fmt::format("{}") -- works great and is a little faster than absl::StrFormat: 21ms.
    //   * fmt::to_string -- works great and is a little faster than fmt::format: 19ms.
    //   * std::to_chars -- fast (16ms) and precise, but requires a few lines to
    //     generate the string_view, and does not work on all platforms yet.
    //
    // The accuracy is checked in buffer_util_test.
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 14000
    // This version is awkward, and doesn't work on all platforms used in Envoy CI
    // as of August 2023, but it is the fastest correct option on modern compilers.
    char buf[100];
    std::to_chars_result result = std::to_chars(buf, buf + sizeof(buf), number);
    ENVOY_BUG(result.ec == std::errc{}, std::make_error_code(result.ec).message());
    buffer.add(absl::string_view(buf, result.ptr - buf));

    // Note: there is room to speed this up further by serializing the number directly
    // into the buffer. However, buffer does not currently make it easy and fast
    // to get (say) 100 characters of raw buffer to serialize into.
#else
    // On older compilers, such as those found on Apple, and gcc, std::to_chars
    // does not work with 'double', so we revert to the next fastest correct
    // implementation.
    buffer.add(fmt::to_string(number));
#endif
  }
};

} // namespace Buffer
} // namespace Envoy
