#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"
#include "test/extensions/filters/listener/tls_inspector/tls_inspector_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

DEFINE_PROTO_FUZZER(
    const test::extensions::filters::listener::tls_inspector::TlsInspectorTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  Stats::IsolatedStoreImpl store;
  ConfigSharedPtr cfg;

  if (input.max_size() == 0) {
    // If max_size not set, use default constructor
    cfg = std::make_shared<Config>(*store.rootScope(), input.config());
  } else {
    cfg = std::make_shared<Config>(*store.rootScope(), input.config(), input.max_size());
  }

  auto filter = std::make_unique<Filter>(std::move(cfg));

  ListenerFilterWithDataFuzzer fuzzer;
  fuzzer.fuzz(std::move(filter), input.fuzzed());
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
