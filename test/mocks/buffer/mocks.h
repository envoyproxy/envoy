#pragma once

#include "common/buffer/buffer_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {

class MockBuffer : public Buffer::OwnedImpl {
public:
  MockBuffer() {
    ON_CALL(*this, write(testing::_))
        .WillByDefault(testing::Invoke(this, &MockBuffer::trackWrites));
    ON_CALL(*this, move(testing::_)).WillByDefault(testing::Invoke(this, &MockBuffer::baseMove));
  }

  MOCK_METHOD1(write, int(int fd));
  MOCK_METHOD1(move, void(Instance& rhs));
  MOCK_METHOD2(move, void(Instance& rhs, uint64_t length));
  MOCK_METHOD1(drain, void(uint64_t size));

  void baseMove(Instance& rhs) { Buffer::OwnedImpl::move(rhs); }
  void baseDrain(uint64_t size) { Buffer::OwnedImpl::drain(size); }

  int trackWrites(int fd) {
    int bytes_written = Buffer::OwnedImpl::write(fd);
    if (bytes_written > 0) {
      bytes_written_ += bytes_written;
    }
    return bytes_written;
  }

  void trackDrains(uint64_t size) {
    bytes_drained_ += size;
    Buffer::OwnedImpl::drain(size);
  }

  // A convenience function to invoke on write() which fails the write with EAGAIN.
  int failWrite(int) {
    errno = EAGAIN;
    return -1;
  }

  int bytes_written() const { return bytes_written_; }
  uint64_t bytes_drained() const { return bytes_drained_; }

private:
  int bytes_written_{0};
  uint64_t bytes_drained_{0};
};

class MockBufferFactory : public Buffer::Factory {
public:
  Buffer::InstancePtr create() override { return Buffer::InstancePtr{create_()}; }

  MOCK_METHOD0(create_, Buffer::Instance*());
};

MATCHER_P(BufferEqual, rhs, testing::PrintToString(*rhs)) {
  return TestUtility::buffersEqual(arg, *rhs);
}

MATCHER_P(BufferStringEqual, rhs, rhs) {
  *result_listener << "\"" << TestUtility::bufferToString(arg) << "\"";

  Buffer::OwnedImpl buffer(rhs);
  return TestUtility::buffersEqual(arg, buffer);
}

ACTION_P(AddBufferToString, target_string) {
  target_string->append(TestUtility::bufferToString(arg0));
  arg0.drain(arg0.length());
}

ACTION_P(AddBufferToStringWithoutDraining, target_string) {
  target_string->append(TestUtility::bufferToString(arg0));
}

} // namespace Envoy
