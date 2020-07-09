#include "envoy/extensions/filters/listener/original_src/v3/original_src.pb.h"

#include "common/network/utility.h"

#include "extensions/filters/listener/original_src/original_src.h"

#include "test/extensions/filters/listener/original_src/original_src_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/fakes.h"

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

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  Network::Address::InstanceConstSharedPtr address = nullptr;

  try {
    address = Network::Utility::resolveUrl(input.address());
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  Network::FakeConnectionSocket socket(nullptr, address);
  ON_CALL(callbacks, socket()).WillByDefault(testing::ReturnRef(socket));

  Config config(input.config());
  auto filter = std::make_unique<OriginalSrcFilter>(config);

  filter->onAccept(callbacks);
}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
