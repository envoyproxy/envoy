#pragma once

#include "test/common/buffer/buffer_fuzz.pb.h"

namespace Envoy {

class BufferFuzz {
public:
  static void bufferFuzz(const test::common::buffer::BufferFuzzTestCase& input);
};

} // namespace Envoy
