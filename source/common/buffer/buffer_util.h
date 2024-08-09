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
  template <class T> static void serializeDouble(double number, T& buffer);
};

} // namespace Buffer
} // namespace Envoy

#include "source/common/buffer/buffer_util.cc"
