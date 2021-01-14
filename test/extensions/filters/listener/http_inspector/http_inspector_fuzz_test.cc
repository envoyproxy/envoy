#include "extensions/filters/listener/http_inspector/http_inspector.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"
#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

DEFINE_PROTO_FUZZER(const test::extensions::filters::listener::FilterFuzzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  Stats::IsolatedStoreImpl store;
  ConfigSharedPtr cfg = std::make_shared<Config>(store);
  auto filter = std::make_unique<Filter>(cfg);

  ListenerFilterFuzzer fuzzer;
  fuzzer.fuzz(*filter, input);
}

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
