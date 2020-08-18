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

  FuzzedHeader header(input);

  if (!header.empty()) {
    ON_CALL(os_sys_calls_, recv(kFakeSocketFd, _, _, _))
        .WillByDefault(testing::Return(Api::SysCallSizeResult{static_cast<ssize_t>(0), 0}));

    ON_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillByDefault(testing::DoAll(testing::SaveArg<1>(&file_event_callback_),
                                      testing::ReturnNew<NiceMock<Event::MockFileEvent>>()));
  }

  filter.onAccept(cb_);

  if (file_event_callback_ == nullptr) {
    // If filter does not call createFileEvent (i.e. original_dst and original_src)
    return;
  }

  if (!header.empty()) {
    ON_CALL(os_sys_calls_, ioctl(kFakeSocketFd, FIONREAD, _))
        .WillByDefault(testing::Return(Api::SysCallIntResult{static_cast<int>(header.size()), 0}));

    {
      testing::InSequence s;

      EXPECT_CALL(os_sys_calls_, recv(kFakeSocketFd, _, _, _))
          .Times(testing::AnyNumber())
          .WillRepeatedly(Invoke(
              [&header](os_fd_t, void* buffer, size_t length, int flags) -> Api::SysCallSizeResult {
                return header.read(buffer, length, flags);
              }));
    }

    bool got_continue = false;

    ON_CALL(cb_, continueFilterChain(true))
        .WillByDefault(testing::InvokeWithoutArgs([&got_continue]() { got_continue = true; }));

    while (!got_continue) {
      if (header.done()) { // End of stream reached but not done
        // file_event_callback_(Event::FileReadyType::Closed);
        return;
      } else {
        file_event_callback_(Event::FileReadyType::Read);
      }

      header.next();
    }
  }
}

FuzzedHeader::FuzzedHeader(const test::extensions::filters::listener::FilterFuzzTestCase& input)
    : nreads_(input.data_size()) {
  size_t len = 0;
  for (int i = 0; i < nreads_; i++) {
    len += input.data(i).size();
  }

  data_.reserve(len);

  for (int i = 0; i < nreads_; i++) {
    data_.insert(data_.end(), input.data(i).begin(), input.data(i).end());
    indices_.push_back(data_.size());
  }
}

void FuzzedHeader::next() {
  if (!done()) {
    nread_++;
  }
}

Api::SysCallSizeResult FuzzedHeader::read(void* buffer, size_t length, int flags) {
  const size_t len = std::min(size(), length); // Number of bytes to write
  memcpy(buffer, data_.data() + index_, len);

  if (flags != MSG_PEEK) {
    // If not peeking, written bytes will be marked as read
    index_ += len;
  }

  return Api::SysCallSizeResult{static_cast<ssize_t>(len), 0};
}

size_t FuzzedHeader::size() { return indices_[nread_] - index_ + 1; }

bool FuzzedHeader::done() { return nread_ >= nreads_; }

bool FuzzedHeader::empty() { return nreads_ == 0 | data_.size() == 0; }

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
