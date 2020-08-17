#include "envoy/network/filter.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fakes.h"
#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.pb.validate.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class ListenerFilterFuzzer {
public:
  ListenerFilterFuzzer() {
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket_));
    ON_CALL(cb_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher_));
  }

  void fuzz(Network::ListenerFilter& filter,
            const test::extensions::filters::listener::FilterFuzzTestCase& input);

private:
  FakeOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  FakeConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::FileReadyCb file_event_callback_;
};

class FuzzedHeader {
public:
  FuzzedHeader(const test::extensions::filters::listener::FilterFuzzTestCase& input);

  // Makes data from the next read available to read()
  void next();

  // Copies data into buffer and returns the number of bytes written
  Api::SysCallSizeResult read(void* buffer, size_t length, int flags);

  size_t size();

  // Returns true if end of stream reached
  bool done();

  // Returns true if data field in proto is empty
  bool empty();

private:
  const int nreads_; // Number of reads
  int nread_ = 0;    // Counter of current read
  size_t index_ = 0; // Index of first unread byte
  std::vector<uint8_t> data_;
  std::vector<size_t> indices_; // Ending indices for each read
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
