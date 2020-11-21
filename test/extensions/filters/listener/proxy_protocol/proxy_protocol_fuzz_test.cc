#include "extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"
#include "test/extensions/filters/listener/proxy_protocol/proxy_protocol_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

DEFINE_PROTO_FUZZER(
    const test::extensions::filters::listener::proxy_protocol::ProxyProtocolTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  Stats::IsolatedStoreImpl store;
  ConfigSharedPtr cfg = std::make_shared<Config>(store, input.config());
  auto filter = std::make_unique<Filter>(std::move(cfg));

  ListenerFilterFuzzer fuzzer;
  fuzzer.fuzz(*filter, input.fuzzed());
}

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
