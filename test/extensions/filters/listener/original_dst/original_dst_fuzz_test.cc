#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.h"

#include "common/network/utility.h"

#include "extensions/filters/listener/original_dst/original_dst.h"

#include "test/extensions/filters/listener/original_dst/original_dst_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {


DEFINE_PROTO_FUZZER(
    const envoy::extensions::filters::listener::original_dst::v3::OriginalDstTestCase& input) {

  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
  try {
    callbacks_.socket_.local_address_ = Network::Utility::resolveUrl(input.address());
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  auto filter = std::make_unique<OriginalDstFilter>();


  try {
    filter->onAccept(callbacks_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
