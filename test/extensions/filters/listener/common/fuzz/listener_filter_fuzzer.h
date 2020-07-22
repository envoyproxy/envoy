#include "envoy/network/filter.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.pb.validate.h"
#include "test/extensions/filters/listener/common/fuzz/listener_filter_fakes.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class ListenerFilterFuzzer {
public:
  ListenerFilterFuzzer(const test::extensions::filters::listener::FilterFuzzTestCase& input) {
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket_));
    try {
      socket_.setLocalAddress(Network::Utility::resolveUrl(input.sock().local_address()));
    } catch (const EnvoyException& e) {
      // If fuzzed local address is malformed or missing, socket's local address will be nullptr
    }
    try {
      socket_.setRemoteAddress(Network::Utility::resolveUrl(input.sock().remote_address()));
    } catch (const EnvoyException& e) {
      // If fuzzed remote address is malformed or missing, socket's remote address will be nullptr
    }
  }

  void fuzz(Network::ListenerFilter& filter) { filter.onAccept(cb_); }

private:
  FakeOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  FakeConnectionSocket socket_;
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
