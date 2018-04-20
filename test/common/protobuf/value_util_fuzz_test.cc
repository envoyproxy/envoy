#include "common/protobuf/utility.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_PROTO_FUZZER(const ProtobufWkt::Value& input) { ValueUtil::equal(input, input); }

} // namespace Fuzz
} // namespace Envoy
