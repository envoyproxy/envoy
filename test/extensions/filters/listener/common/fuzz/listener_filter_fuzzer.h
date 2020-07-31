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
  ListenerFilterFuzzer() {
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket_));
    ON_CALL(cb_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher_));
  }

  void fuzz(Network::ListenerFilter& filter, const test::extensions::filters::listener::FilterFuzzTestCase& input) {
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

    const int nreads = input.data_size(); // Number of reads

    if (nreads > 0) {
      EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(testing::Return("raw_buffer"));

      EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
          .WillOnce(testing::Return(Api::SysCallSizeResult{static_cast<ssize_t>(0), 0}));

      EXPECT_CALL(dispatcher_, createFileEvent_(_, _, Event::FileTriggerType::Edge,
                                                Event::FileReadyType::Read | Event::FileReadyType::Closed))
          .WillOnce(testing::DoAll(testing::SaveArg<1>(&file_event_callback_),
                                   testing::ReturnNew<NiceMock<Event::MockFileEvent>>()));
    }

    filter.onAccept(cb_);

    if (nreads > 0) {
      // Construct header from single or multiple reads
      std::string data = "";
      size_t curr = 0;
      std::vector<size_t> indices; // Ending indices for each read

      for (int i = 0; i < nreads; i++) {
        data += input.data(i);
        curr += input.data(i).size();
        indices.push_back(curr);
      }

      absl::string_view header(data);

      bool end_stream = false;

      int nread = 0;

      {
        testing::InSequence s;

        if (nreads > 1) {
          EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).WillOnce(testing::InvokeWithoutArgs([]() {
            return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
          }));
        }

        EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK)).Times(testing::AnyNumber())
          .WillRepeatedly(
              Invoke([&header, &indices, &end_stream, &nread, nreads](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
                ASSERT(length >= indices[nread]);
                memcpy(buffer, header.data(), indices[nread]);
                if (++nread == nreads) {
                  end_stream = true;
                }

                return Api::SysCallSizeResult{ssize_t(indices[nread - 1]), 0};
              }));
      }

      bool got_continue = false;

      ON_CALL(cb_, continueFilterChain(true)).WillByDefault(testing::InvokeWithoutArgs([&got_continue]() {
        got_continue = true;
      }));

      while (!end_stream && !got_continue) {
        file_event_callback_(Event::FileReadyType::Read);
      }
    }
  }

private:
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
