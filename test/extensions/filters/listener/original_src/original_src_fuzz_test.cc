#include "extensions/filters/listener/original_src/original_src.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"
#include "test/extensions/filters/listener/original_src/original_src_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

DEFINE_PROTO_FUZZER(
    const test::extensions::filters::listener::original_src::OriginalSrcTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  Config config(input.config());
  auto filter = std::make_unique<OriginalSrcFilter>(config);
  ListenerFilterFuzzer fuzzer;
  fuzzer.fuzz(*filter, input.fuzzed());
}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
