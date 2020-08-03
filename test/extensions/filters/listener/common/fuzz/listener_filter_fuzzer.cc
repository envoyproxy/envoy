#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

void ListenerFilterFuzzer::fuzz(
    Network::ListenerFilter& filter,
    const test::extensions::filters::listener::FilterFuzzTestCase& input) {
  try {
    socket_.setLocalAddress(Network::Utility::resolveUrl(input.sock().local_address()));
  } catch (const EnvoyException& e) {
    // Socket's local address will be nullptr by default if fuzzed local address is malformed
    // or missing - local address field in proto is optional
  }
  try {
    socket_.setRemoteAddress(Network::Utility::resolveUrl(input.sock().remote_address()));
  } catch (const EnvoyException& e) {
    // Socket's remote address will be nullptr by default if fuzzed remote address is malformed
    // or missing - remote address field in proto is optional
  }

  const int nreads = input.data_size(); // Number of reads

  if (nreads > 0) {
    EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(testing::Return("raw_buffer"));

    EXPECT_CALL(os_sys_calls_, recv(FAKE_SOCKET_FD, _, _, MSG_PEEK))
        .WillOnce(testing::Return(Api::SysCallSizeResult{static_cast<ssize_t>(0), 0}));

    EXPECT_CALL(dispatcher_,
                createFileEvent_(_, _, Event::FileTriggerType::Edge,
                                 Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillOnce(testing::DoAll(testing::SaveArg<1>(&file_event_callback_),
                                 testing::ReturnNew<NiceMock<Event::MockFileEvent>>()));
  }

  filter.onAccept(cb_);

  if (nreads > 0) {
    // Construct header from single or multiple reads
    std::string header = "";
    std::vector<size_t> indices; // Ending indices for each read

    for (int i = 0; i < nreads; i++) {
      header += input.data(i);
      indices.push_back(header.size());
    }

    int nread = 0; // Counter of current read

    {
      testing::InSequence s;

      if (nreads > 1) {
        EXPECT_CALL(os_sys_calls_, recv(FAKE_SOCKET_FD, _, _, MSG_PEEK))
            .WillOnce(testing::InvokeWithoutArgs([]() {
              return Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN};
            }));
      }

      EXPECT_CALL(os_sys_calls_, recv(FAKE_SOCKET_FD, _, _, MSG_PEEK))
          .Times(testing::AnyNumber())
          .WillRepeatedly(Invoke([&header, &indices, &nread](os_fd_t, void* buffer, size_t length,
                                                             int) -> Api::SysCallSizeResult {
            ASSERT(length >= indices[nread]);
            memcpy(buffer, header.data(), indices[nread]);
            return Api::SysCallSizeResult{ssize_t(indices[nread++]), 0};
          }));
    }

    bool got_continue = false;

    ON_CALL(cb_, continueFilterChain(true))
        .WillByDefault(testing::InvokeWithoutArgs([&got_continue]() { got_continue = true; }));

    while (!got_continue) {
      if (nread >= nreads) { // End of stream reached but not done
        nread--;             // Decrement to avoid out-of-range for last recv() call
        file_event_callback_(Event::FileReadyType::Closed);
        break;
      } else {
        file_event_callback_(Event::FileReadyType::Read);
      }
    }
  }
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
