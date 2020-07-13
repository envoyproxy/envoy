#include "test/extensions/filters/listener/common/listener_filter_fuzz.pb.validate.h"
#include "test/extensions/filters/listener/common/uber_filter.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

DEFINE_PROTO_FUZZER(const test::extensions::filters::listener::FilterFuzzTestCase& input) {
  try {
    TestUtility::validate(input);
    static UberFilterFuzzer fuzzer;
    fuzzer.fuzz(input.config(), input.data());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
