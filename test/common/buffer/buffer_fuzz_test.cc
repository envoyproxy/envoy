#include "test/common/buffer/buffer_fuzz.h"
#include "test/common/buffer/buffer_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {

// Fuzz the old owned buffer implementation.
DEFINE_PROTO_FUZZER(const test::common::buffer::BufferFuzzTestCase& input) {
  Envoy::BufferFuzz::bufferFuzz(input);
}

} // namespace Envoy
