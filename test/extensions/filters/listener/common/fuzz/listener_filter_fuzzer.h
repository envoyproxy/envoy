#include "common/common/hex.h"
#include "envoy/network/filter.h"
#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.pb.validate.h"
#include "test/extensions/filters/listener/common/fuzz/listener_filter_fakes.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class ListenerFilterFuzzer {
public:
  ListenerFilterFuzzer(const test::extensions::filters::listener::FilterFuzzTestCase& input)
      : data_(input.data()) {
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket_));
    ON_CALL(cb_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher_));

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

    if (!data_.empty()) {
      EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(testing::Return("raw_buffer"));

      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(testing::Return(Api::SysCallSizeResult{static_cast<ssize_t>(0), 0}));

      EXPECT_CALL(dispatcher_, createFileEvent_(_, _, Event::FileTriggerType::Edge,
                                                Event::FileReadyType::Read | Event::FileReadyType::Closed))
          .WillOnce(testing::DoAll(testing::SaveArg<1>(&file_event_callback_),
                                   testing::ReturnNew<NiceMock<Event::MockFileEvent>>()));
    }
  }

  void fuzz(Network::ListenerFilter& filter) {
    filter.onAccept(cb_);

    if (!data_.empty()) {
      auto& header = data_;
      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(
          Invoke([&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
          }));

      if (file_event_callback_ != nullptr) {
        file_event_callback_(Event::FileReadyType::Read);
      }
    }
  }

private:
  absl::string_view data_;
  FakeOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  FakeConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::FileReadyCb file_event_callback_;
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
