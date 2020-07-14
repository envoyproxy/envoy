// #include "common/network/utility.h"

#include "extensions/filters/listener/original_dst/original_dst.h"

#include "test/extensions/filters/listener/common/listener_filter_fuzz_test.pb.validate.h"
#include "test/extensions/filters/listener/common/uber_filter.h"

// #include "test/extensions/filters/listener/original_dst/original_dst_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
// #include "test/mocks/network/mocks.h"
// #include "test/mocks/network/fakes.h"

// #include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

DEFINE_PROTO_FUZZER(
    const test::extensions::filters::listener::FilterFuzzTestCase& input) {

  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  auto filter = std::make_unique<OriginalDstFilter>();

  try {
    UberFilterFuzzer fuzzer;
    fuzzer.fuzz(*filter, input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }

  /*
  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  Network::Address::InstanceConstSharedPtr address = nullptr;

  try {
    address = Network::Utility::resolveUrl(input.address());
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  Network::FakeConnectionSocket socket(address, nullptr);
  ON_CALL(callbacks, socket()).WillByDefault(testing::ReturnRef(socket));

  auto filter = std::make_unique<OriginalDstFilter>();
  filter->onAccept(callbacks);
  */
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
