#include "source/extensions/filters/listener/original_dst/original_dst.h"

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
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Exception: {}", e.what());
    return;
  }

  auto filter =
      std::make_unique<OriginalDstFilter>(envoy::config::core::v3::TrafficDirection::UNSPECIFIED);
  ListenerFilterFuzzer fuzzer;
  fuzzer.fuzz(std::move(filter), input);
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
