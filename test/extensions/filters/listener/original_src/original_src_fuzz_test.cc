#include "envoy/extensions/filters/listener/original_src/v3/original_src.pb.h"

#include "common/network/utility.h"

#include "extensions/filters/listener/original_src/original_src.h"

#include "test/extensions/filters/listener/original_src/original_src_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

DEFINE_PROTO_FUZZER(
    const envoy::extensions::filters::listener::original_src::OriginalSrcTestCase& input) {

  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
  try {
    callbacks_.socket_.remote_address_ = Network::Utility::resolveUrl(input.address());
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  Config config(input.config());
  auto filter = std::make_unique<OriginalSrcFilter>(config);

  filter->onAccept(callbacks_);
}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
