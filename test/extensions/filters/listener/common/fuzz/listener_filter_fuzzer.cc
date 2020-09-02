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
    socket_.setLocalAddress(Network::Utility::resolveUrl("tcp://0.0.0.0:0"));
  }
  try {
    socket_.setRemoteAddress(Network::Utility::resolveUrl(input.sock().remote_address()));
  } catch (const EnvoyException& e) {
    socket_.setRemoteAddress(Network::Utility::resolveUrl("tcp://0.0.0.0:0"));
  }

  FuzzedInputStream data(input);

  if (!data.empty()) {
    ON_CALL(os_sys_calls_, recv(kFakeSocketFd, _, _, _))
        .WillByDefault(testing::Return(Api::SysCallSizeResult{static_cast<ssize_t>(0), 0}));

    ON_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillByDefault(testing::DoAll(testing::SaveArg<1>(&file_event_callback_),
                                      testing::SaveArg<3>(&events_),
                                      testing::ReturnNew<NiceMock<Event::MockFileEvent>>()));
  }

  filter.onAccept(cb_);

  if (file_event_callback_ == nullptr) {
    // If filter does not call createFileEvent (i.e. original_dst and original_src)
    return;
  }

  if (!data.empty()) {
    ON_CALL(os_sys_calls_, ioctl(kFakeSocketFd, FIONREAD, _))
        .WillByDefault(
            Invoke([&data](os_fd_t, unsigned long int, void* argp) -> Api::SysCallIntResult {
              int bytes_avail = static_cast<int>(data.size());
              memcpy(argp, &bytes_avail, sizeof(int));
              return Api::SysCallIntResult{bytes_avail, 0};
            }));
    {
      testing::InSequence s;

      EXPECT_CALL(os_sys_calls_, recv(kFakeSocketFd, _, _, _))
          .Times(testing::AnyNumber())
          .WillRepeatedly(Invoke(
              [&data](os_fd_t, void* buffer, size_t length, int flags) -> Api::SysCallSizeResult {
                return data.read(buffer, length, flags == MSG_PEEK);
              }));
    }

    bool got_continue = false;

    ON_CALL(cb_, continueFilterChain(true))
        .WillByDefault(testing::InvokeWithoutArgs([&got_continue]() { got_continue = true; }));

    while (!got_continue) {
      if (data.done()) { // End of stream reached but not done
        if (events_ & Event::FileReadyType::Closed) {
          file_event_callback_(Event::FileReadyType::Closed);
        }
        return;
      } else {
        file_event_callback_(Event::FileReadyType::Read);
      }

      data.next();
    }
  }
}

FuzzedInputStream::FuzzedInputStream(
    const test::extensions::filters::listener::FilterFuzzTestCase& input)
    : nreads_(input.data_size()) {
  size_t len = 0;
  for (int i = 0; i < nreads_; i++) {
    len += input.data(i).size();
  }

  data_.reserve(len);

  for (int i = 0; i < nreads_; i++) {
    data_.insert(data_.end(), input.data(i).begin(), input.data(i).end());
    indices_.push_back(data_.size() - 1);
  }
}

FuzzedInputStream::FuzzedInputStream(std::vector<uint8_t> buffer, std::vector<size_t> indices)
    : nreads_(indices.size()), data_(std::move(buffer)), indices_(std::move(indices)) {}

void FuzzedInputStream::next() {
  if (!done()) {
    nread_++;
  }
}

Api::SysCallSizeResult FuzzedInputStream::read(void* buffer, size_t length, bool peek) {
  const size_t len = std::min(size(), length); // Number of bytes to write
  memcpy(buffer, data_.data() + index_, len);

  if (!peek) {
    // If not peeking, written bytes will be marked as read
    index_ += len;
  }

  return Api::SysCallSizeResult{static_cast<ssize_t>(len), 0};
}

size_t FuzzedInputStream::size() const { return indices_[nread_] - index_ + 1; }

bool FuzzedInputStream::done() { return nread_ >= nreads_ - 1; }

bool FuzzedInputStream::empty() { return nreads_ == 0 || data_.empty(); }

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
