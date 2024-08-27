#include "envoy/api/io_error.h"

#include "source/common/network/listener_filter_buffer_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/io_handle.h"

#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Network {
namespace {

class ListenerFilterBufferImplTest : public testing::Test {
public:
  void initialize() {
    EXPECT_CALL(io_handle_,
                createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                 Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillOnce(SaveArg<1>(&file_event_callback_));

    listener_buffer_ = std::make_unique<ListenerFilterBufferImpl>(
        io_handle_, dispatcher_,
        [&](bool error) {
          if (on_close_cb_) {
            on_close_cb_(error);
          }
        },
        [&](ListenerFilterBufferImpl& filter_buffer) {
          if (on_data_cb_) {
            on_data_cb_(filter_buffer);
          }
        },
        buffer_size_ == 0, buffer_size_);
  }
  std::unique_ptr<ListenerFilterBufferImpl> listener_buffer_;
  Network::MockIoHandle io_handle_;
  Event::MockDispatcher dispatcher_;
  uint64_t buffer_size_{512};
  ListenerFilterBufferOnDataCb on_data_cb_;
  ListenerFilterBufferOnCloseCb on_close_cb_;
  Event::FileReadyCb file_event_callback_;
};

TEST_F(ListenerFilterBufferImplTest, Basic) {
  initialize();

  // Peek 256 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = 0; i < length / 2; i++) {
      buf[i] = 'a';
    }
    return Api::IoCallUint64Result(length / 2, Api::IoError::none());
  });
  on_data_cb_ = [&](ListenerFilterBuffer& filter_buffer) {
    auto raw_buffer = filter_buffer.rawSlice();
    EXPECT_EQ(buffer_size_ / 2, raw_buffer.len_);
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < raw_buffer.len_; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
  };
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  // Peek another 256 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = length / 2; i < length; i++) {
      buf[i] = 'b';
    }
    return Api::IoCallUint64Result(length, Api::IoError::none());
  });

  on_data_cb_ = [&](ListenerFilterBuffer& filter_buffer) {
    auto raw_buffer = filter_buffer.rawSlice();
    EXPECT_EQ(buffer_size_, raw_buffer.len_);
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < buffer_size_ / 2; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
    for (uint64_t i = buffer_size_ / 2; i < buffer_size_; i++) {
      EXPECT_EQ(buf[i], 'b');
    }
  };
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  // On socket failure
  bool is_closed = false;
  on_close_cb_ = [&](bool) { is_closed = true; };
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(
          Return(ByMove(Api::IoCallUint64Result(-1, IoSocketError::create(SOCKET_ERROR_INTR)))));
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  EXPECT_TRUE(is_closed);

  // On remote closed
  is_closed = false;
  on_close_cb_ = [&](bool) { is_closed = true; };
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(0, Api::IoError::none()))));
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  EXPECT_TRUE(is_closed);

  // On socket again.
  is_closed = false;
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(
          Return(ByMove(Api::IoCallUint64Result(0, IoSocketError::getIoSocketEagainError()))));
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
  EXPECT_FALSE(is_closed);
}

TEST_F(ListenerFilterBufferImplTest, ZeroBuffer) {
  buffer_size_ = 0;
  initialize();

  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    // No matter how much data is ready, only 1 byte will be peeked into buffer
    EXPECT_EQ(length, 1);
    char* buf = static_cast<char*>(buffer);
    buf[0] = 'a';
    return Api::IoCallUint64Result(1, Api::IoError::none());
  });
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  listener_buffer_->resetCapacity(1024);

  EXPECT_EQ(1024, listener_buffer_->capacity());
  EXPECT_EQ(0, listener_buffer_->rawSlice().len_);

  // Peek 1024 bytes data for next listener filter with buffer size of 1024.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(1024, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = 0; i < length; i++) {
      buf[i] = 'a';
    }
    return Api::IoCallUint64Result(length, Api::IoError::none());
  });

  on_data_cb_ = [&](ListenerFilterBuffer& filter_buffer) {
    auto raw_buffer = filter_buffer.rawSlice();
    EXPECT_EQ(1024, raw_buffer.len_);
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < raw_buffer.len_; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
  };
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
}

TEST_F(ListenerFilterBufferImplTest, DrainData) {
  initialize();

  // Peek 256 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = 0; i < length / 2; i++) {
      buf[i] = 'a';
    }
    return Api::IoCallUint64Result(length / 2, Api::IoError::none());
  });
  on_data_cb_ = [&](ListenerFilterBuffer& filter_buffer) {
    auto raw_buffer = filter_buffer.rawSlice();
    EXPECT_EQ(buffer_size_ / 2, raw_buffer.len_);
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < raw_buffer.len_; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
  };
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  // Drain the 128 bytes data
  uint64_t drained_size = 128;

  // Drain the data from the actual socket
  EXPECT_CALL(io_handle_, recv)
      .WillOnce([&](void*, size_t length, int flags) {
        // expect to read, not peek
        EXPECT_EQ(0, flags);
        // expect to read the `drained_size` data
        EXPECT_EQ(drained_size, length);
        // only drain half data from the socket.
        return Api::IoCallUint64Result(drained_size / 2, Api::IoError::none());
      })
      .WillOnce([&](void*, size_t length, int flags) {
        // expect to read, not peek
        EXPECT_EQ(0, flags);
        // expect to read the `drained_size` data
        EXPECT_EQ(drained_size / 2, length);
        return Api::IoCallUint64Result(drained_size / 2, Api::IoError::none());
      });

  listener_buffer_->drain(drained_size);
  // Then should only can access the last 128 bytes
  auto slice1 = listener_buffer_->rawSlice();
  EXPECT_EQ(drained_size, slice1.len_);
  const char* buf = static_cast<const char*>(slice1.mem_);
  for (uint64_t i = 0; i < drained_size; i++) {
    EXPECT_EQ(buf[i], 'a');
  }

  // Peek again
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (uint64_t i = 0; i < length; i++) {
      buf[i] = 'b';
    }
    return Api::IoCallUint64Result(length, Api::IoError::none());
  });
  on_data_cb_ = [&](ListenerFilterBuffer& filter_buffer) {
    auto raw_slice = filter_buffer.rawSlice();
    EXPECT_EQ(buffer_size_, raw_slice.len_);
    buf = static_cast<const char*>(raw_slice.mem_);
    for (uint64_t i = 0; i < raw_slice.len_; i++) {
      EXPECT_EQ(buf[i], 'b');
    }
  };
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
}

TEST_F(ListenerFilterBufferImplTest, ResetCapacity) {
  initialize();

  // Peek 256 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = 0; i < length / 2; i++) {
      buf[i] = 'a';
    }
    return Api::IoCallUint64Result(length / 2, Api::IoError::none());
  });
  on_data_cb_ = [&](ListenerFilterBuffer& filter_buffer) {
    auto raw_buffer = filter_buffer.rawSlice();
    EXPECT_EQ(buffer_size_ / 2, raw_buffer.len_);
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < raw_buffer.len_; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
  };
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());

  listener_buffer_->resetCapacity(1024);

  EXPECT_EQ(1024, listener_buffer_->capacity());
  EXPECT_EQ(0, listener_buffer_->rawSlice().len_);

  // Peek 1024 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(1024, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = 0; i < length; i++) {
      buf[i] = 'b';
    }
    return Api::IoCallUint64Result(length, Api::IoError::none());
  });

  on_data_cb_ = [&](ListenerFilterBuffer& filter_buffer) {
    auto raw_buffer = filter_buffer.rawSlice();
    EXPECT_EQ(1024, raw_buffer.len_);
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < raw_buffer.len_; i++) {
      EXPECT_EQ(buf[i], 'b');
    }
  };
  EXPECT_TRUE(file_event_callback_(Event::FileReadyType::Read).ok());
}

} // namespace
} // namespace Network
} // namespace Envoy
