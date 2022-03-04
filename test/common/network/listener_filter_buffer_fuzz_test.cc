#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/network/listener_filter_buffer_impl.h"

#include "test/common/network/listener_filter_buffer_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/io_handle.h"

#include "gtest/gtest.h"

using testing::_;
using testing::SaveArg;

namespace Envoy {
namespace Network {
namespace {

// The max size of the listener filter buffer.
constexpr uint32_t max_buffer_size = 16 * 1024;
// The max size of available data on the socket. It can be large than
// buffer size, but we won't peek those extra data.
constexpr uint32_t max_readable_size = max_buffer_size + 1024;

class ListenerFilterBufferFuzzer {
public:
  ListenerFilterBufferFuzzer() : drained_size_(0) {}

  void fuzz(const test::common::network::ListenerFilterBufferFuzzTestCase& input) {
    // Ensure the buffer is not exceed the limit we set.
    auto max_bytes_read = input.max_bytes_read() % max_buffer_size;
    // There won't be any case the max size of buffer is 0.
    if (max_bytes_read == 0) {
      return;
    }

    EXPECT_CALL(io_handle_, createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                             Event::FileReadyType::Read))
        .WillOnce(SaveArg<1>(&file_event_callback_));

    // Use the on_data callback to verify the data.
    auto on_data_cb = [&](ListenerFilterBuffer& buffer) {
      auto raw_slice = buffer.rawSlice();
      std::string data(reinterpret_cast<const char*>(raw_slice.mem_), raw_slice.len_);
      // The available data may be more than the buffer size, also, the buffer size
      // can be reduced by drain.
      FUZZ_ASSERT(data == available_data_.substr(0, max_bytes_read - drained_size_));
    };
    auto listener_buffer = std::make_unique<ListenerFilterBufferImpl>(
        io_handle_, dispatcher_, [&](bool) {}, on_data_cb, max_bytes_read);

    for (auto i = 0; i < input.actions().size(); i++) {
      const char insert_value = 'a' + i % 26;

      switch (input.actions(i).action_selector_case()) {
      case test::common::network::Action::kReadable: {
        // If the available is 0, it means nothing to read, then skip it.
        if (input.actions(i).readable() == 0) {
          break;
        }

        // Generate the available data, and ensure it is under the max_readable_size.
        auto append_data_size =
            input.actions(i).readable() % (max_readable_size - available_data_.size());
        available_data_.insert(available_data_.end(), append_data_size, insert_value);
        EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
          EXPECT_EQ(MSG_PEEK, flags);
          // The peek size will be reduced due to the drain.
          EXPECT_EQ(max_bytes_read - drained_size_, length);
          auto copy_size = std::min(length, available_data_.size());
          ::memcpy(buffer, available_data_.data(), copy_size);
          return Api::IoCallUint64Result(copy_size, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
        });
        // Trigger the peek by event.
        file_event_callback_(Event::FileReadyType::Read);
        break;
      }
      case test::common::network::Action::kDrain: {
        // The drain method only support drain size less than the buffer size.
        auto drain_size = std::min(input.actions(i).drain(), listener_buffer->rawSlice().len_);
        if (drain_size != 0) {
          EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
            EXPECT_EQ(0, flags);
            EXPECT_EQ(drain_size, length);
            ::memcpy(buffer, available_data_.data(), drain_size);
            available_data_ = available_data_.substr(drain_size);
            return Api::IoCallUint64Result(drain_size,
                                           Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
          });
        }
        drained_size_ += drain_size;
        listener_buffer->drain(drain_size);
        // Reuse the on_data callback to validate the buffer data.
        on_data_cb(*listener_buffer);
        break;
      }
      default:
        break;
      }
    }
  }

private:
  Network::MockIoHandle io_handle_;
  Event::MockDispatcher dispatcher_;
  Event::FileReadyCb file_event_callback_;
  std::string available_data_;
  // The size drained by the test. This is used to calculate the current buffer size.
  uint64_t drained_size_;
};

DEFINE_PROTO_FUZZER(const test::common::network::ListenerFilterBufferFuzzTestCase& input) {
  auto fuzzer = ListenerFilterBufferFuzzer();
  fuzzer.fuzz(input);
}

} // namespace
} // namespace Network
} // namespace Envoy
