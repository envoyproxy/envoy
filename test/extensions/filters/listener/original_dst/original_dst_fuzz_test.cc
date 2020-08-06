#include "extensions/filters/listener/original_dst/original_dst.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"
#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

DEFINE_PROTO_FUZZER(const test::extensions::filters::listener::FilterFuzzTestCase& input) {

  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  auto filter = std::make_unique<OriginalDstFilter>();

  try {
    ListenerFilterFuzzer fuzzer;
    fuzzer.fuzz(*filter, input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
