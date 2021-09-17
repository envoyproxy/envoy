#include "envoy/api/io_error.h"

#include "source/common/network/listener_filter_buffer_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/io_handle.h"

#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::NiceMock;
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
        [&]() {
          if (on_close_cb_) {
            on_close_cb_();
          }
        },
        [&]() {
          if (on_data_cb_) {
            on_data_cb_();
          }
        },
        buffer_size_);
  }
  std::unique_ptr<ListenerFilterBufferImpl> listener_buffer_;
  Network::MockIoHandle io_handle_;
  Event::MockDispatcher dispatcher_;
  uint64_t buffer_size_{512};
  ListenerFilterBufferOnDataCb on_close_cb_;
  ListenerFilterBufferOnCloseCb on_data_cb_;
  Event::FileReadyCb file_event_callback_;
};

TEST_F(ListenerFilterBufferImplTest, Basic) {
  initialize();

  // peek 256 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = 0; i < length / 2; i++) {
      buf[i] = 'a';
    }
    return Api::IoCallUint64Result(length / 2, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
  });
  on_data_cb_ = [&]() {
    EXPECT_EQ(buffer_size_ / 2, listener_buffer_->length());
    auto raw_buffer = listener_buffer_->rawSlice();
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < raw_buffer.len_; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
  };
  file_event_callback_(Event::FileReadyType::Read);

  // peek another 256 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = length / 2; i < length; i++) {
      buf[i] = 'b';
    }
    return Api::IoCallUint64Result(length, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
  });

  on_data_cb_ = [&]() {
    EXPECT_EQ(buffer_size_, listener_buffer_->length());
    auto raw_buffer = listener_buffer_->rawSlice();
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < buffer_size_ / 2; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
    for (uint64_t i = buffer_size_ / 2; i < buffer_size_; i++) {
      EXPECT_EQ(buf[i], 'b');
    }
  };
  file_event_callback_(Event::FileReadyType::Read);

  // on socket failure
  bool is_closed = false;
  on_close_cb_ = [&]() { is_closed = true; };
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(
          ByMove(Api::IoCallUint64Result(-1, Api::IoErrorPtr(new IoSocketError(SOCKET_ERROR_INTR),
                                                             IoSocketError::deleteIoError)))));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_TRUE(is_closed);

  // on remote closed
  is_closed = false;
  on_close_cb_ = [&]() { is_closed = true; };
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(
          ByMove(Api::IoCallUint64Result(0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_TRUE(is_closed);

  // on socket again.
  is_closed = false;
  EXPECT_CALL(io_handle_, recv)
      .WillOnce(Return(ByMove(
          Api::IoCallUint64Result(0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                                     IoSocketError::deleteIoError)))));
  file_event_callback_(Event::FileReadyType::Read);
  EXPECT_FALSE(is_closed);

  // close the socket.
  is_closed = true;
  file_event_callback_(Event::FileReadyType::Closed);
  EXPECT_TRUE(is_closed);
}

TEST_F(ListenerFilterBufferImplTest, DrainData) {
  initialize();

  // peek 256 bytes data.
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (size_t i = 0; i < length / 2; i++) {
      buf[i] = 'a';
    }
    return Api::IoCallUint64Result(length / 2, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
  });
  on_data_cb_ = [&]() {
    EXPECT_EQ(buffer_size_ / 2, listener_buffer_->length());
    auto raw_buffer = listener_buffer_->rawSlice();
    const char* buf = static_cast<const char*>(raw_buffer.mem_);
    for (uint64_t i = 0; i < raw_buffer.len_; i++) {
      EXPECT_EQ(buf[i], 'a');
    }
  };
  file_event_callback_(Event::FileReadyType::Read);

  // drain the 128 bytes data
  uint64_t drained_size = 128;
  listener_buffer_->drain(drained_size);
  // then should only can access the last 128 bytes
  auto slice1 = listener_buffer_->rawSlice();
  EXPECT_EQ(drained_size, slice1.len_);
  const char* buf = static_cast<const char*>(slice1.mem_);
  for (uint64_t i = 0; i < drained_size; i++) {
    EXPECT_EQ(buf[i], 'a');
  }

  // peek again
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    EXPECT_EQ(MSG_PEEK, flags);
    EXPECT_EQ(buffer_size_, length);
    char* buf = static_cast<char*>(buffer);
    for (uint64_t i = 0; i < buffer_size_; i++) {
      buf[i] = 'b';
    }
    return Api::IoCallUint64Result(length, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
  });
  on_data_cb_ = [&]() {
    auto raw_slice = listener_buffer_->rawSlice();
    EXPECT_EQ(buffer_size_ - drained_size, raw_slice.len_);
    buf = static_cast<const char*>(raw_slice.mem_);
    for (uint64_t i = 0; i < raw_slice.len_; i++) {
      EXPECT_EQ(buf[i], 'b');
    }
  };
  file_event_callback_(Event::FileReadyType::Read);

  // drain the data from the actual socket
  EXPECT_CALL(io_handle_, recv)
      .WillOnce([&](void*, size_t length, int flags) {
        // expect to read, not peek
        EXPECT_EQ(0, flags);
        // expect to read the `drained_size` data
        EXPECT_EQ(drained_size, length);
        // only drain half data from the socket.
        return Api::IoCallUint64Result(drained_size / 2,
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      })
      .WillOnce([&](void*, size_t length, int flags) {
        // expect to read, not peek
        EXPECT_EQ(0, flags);
        // expect to read the `drained_size` data
        EXPECT_EQ(drained_size / 2, length);
        return Api::IoCallUint64Result(drained_size / 2,
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      });
  listener_buffer_->drainFromSocket();
  auto slice2 = listener_buffer_->rawSlice();
  EXPECT_EQ(buffer_size_ - drained_size, slice2.len_);

  // peek again after drain the data from actual socket
  EXPECT_CALL(io_handle_, recv).WillOnce([&](void* buffer, size_t length, int flags) {
    // expect to read, not peek
    EXPECT_EQ(MSG_PEEK, flags);
    // expect to read the `drained_size` data
    EXPECT_EQ(buffer_size_ - drained_size, length);
    char* buf = static_cast<char*>(buffer);
    for (uint64_t i = 0; i < buffer_size_ - drained_size; i++) {
      buf[i] = 'x';
    }
    return Api::IoCallUint64Result(buffer_size_ - drained_size,
                                   Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
  });
  on_data_cb_ = [&]() {
    auto raw_slice = listener_buffer_->rawSlice();
    EXPECT_EQ(buffer_size_ - drained_size, raw_slice.len_);
    const char* buf = static_cast<const char*>(raw_slice.mem_);
    for (uint64_t i = 0; i < raw_slice.len_; i++) {
      EXPECT_EQ(buf[i], 'x');
    }
  };
  file_event_callback_(Event::FileReadyType::Read);
}

} // namespace
} // namespace Network
} // namespace Envoy