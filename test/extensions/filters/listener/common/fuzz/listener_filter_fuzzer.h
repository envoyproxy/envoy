#include "envoy/network/filter.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.pb.validate.h"
#include "test/mocks/network/fakes.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class ListenerFilterFuzzer {
public:
  void fuzz(Network::ListenerFilter& filter,
            const test::extensions::filters::listener::FilterFuzzTestCase& input);

private:
  void fuzzerSetup(const test::extensions::filters::listener::FilterFuzzTestCase& input) {
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket_));
    socketSetup(input);
  }

  void socketSetup(const test::extensions::filters::listener::FilterFuzzTestCase& input);

  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  Network::FakeConnectionSocket socket_;
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
