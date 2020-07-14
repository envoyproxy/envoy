#include "test/extensions/filters/listener/common/uber_filter.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

void UberFilterFuzzer::fuzz(Network::ListenerFilter& filter,
                            const test::extensions::filters::listener::FilterFuzzTestCase& input) {
  try {
    fuzzerSetup(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  filter.onAccept(cb_);
}

void UberFilterFuzzer::socketSetup(
    const test::extensions::filters::listener::FilterFuzzTestCase& input) {
  socket_.setLocalAddress(Network::Utility::resolveUrl(input.sock().local_address()));
  // socket_.setRemoteAddress(Network::Utility::resolveUrl(input.sock().remote_address()));
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
