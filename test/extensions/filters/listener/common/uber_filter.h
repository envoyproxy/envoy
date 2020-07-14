#include "test/extensions/filters/listener/common/listener_filter_fuzz_test.pb.validate.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/fakes.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer(const std::unique_ptr<ListenerFilter>& filter,
                   const envoy::extensions::filters::listener::FilterFuzzTestCase& input)
      : filter_(filter)
      , socket_(socketSetup(input)) {}

  void fuzz();

private:
  Network::FakeConnectionSocket socketSetup(
      const envoy::extensions::filters::listener::FilterFuzzTestCase& input);

  std::unique_ptr<ListenerFilter> filter_;
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  Network::FakeConnectionSocket socket_;
  // NiceMock<Event::MockDispatcher> dispatcher_;
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
