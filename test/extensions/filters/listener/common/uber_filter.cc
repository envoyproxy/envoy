#include "test/extensions/filters/listener/common/uber_filter.h"

#include "test/extensions/filters/listener/common/listener_filter_fuzz_test.pb.validate.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/fakes.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

void UberFilterFuzzer::fuzz() {
  filter_->onAccept(cb_);
}

Network::FakeConnectionSocket UberFilterFuzzer::socketSetup(
    const envoy::extensions::filters::listener::FilterFuzzTestCase& input) {
  try {
    Network::FakeConnectionSocket socket(Network::Utility::resolveUrl(input.local_address),
                                         Network::Utility::resolveUrl(input.remote_address));
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket));
    return socket;
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}");
  }
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
