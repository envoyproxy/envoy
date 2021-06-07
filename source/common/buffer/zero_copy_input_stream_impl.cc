#include "source/common/buffer/zero_copy_input_stream_impl.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"

namespace Envoy {
namespace Buffer {

ZeroCopyInputStreamImpl::ZeroCopyInputStreamImpl() : buffer_(new Buffer::OwnedImpl) {}

ZeroCopyInputStreamImpl::ZeroCopyInputStreamImpl(Buffer::InstancePtr&& buffer)
    : buffer_(std::move(buffer)) {
  finish();
}

void ZeroCopyInputStreamImpl::move(Buffer::Instance& instance) {
  ASSERT(!finished_);

  buffer_->move(instance);
}

void ZeroCopyInputStreamImpl::drainLastSlice() {
  if (position_ != 0) {
    buffer_->drain(position_);
    position_ = 0;
  }
}

bool ZeroCopyInputStreamImpl::Next(const void** data, int* size) {
  drainLastSlice();

  Buffer::RawSlice slice = buffer_->frontSlice();

  if (slice.len_ > 0) {
    *data = slice.mem_;
    *size = slice.len_;
    position_ = slice.len_;
    byte_count_ += slice.len_;
    return true;
  }

  if (!finished_) {
    *data = nullptr;
    *size = 0;
    return true;
  }
  return false;
}

bool ZeroCopyInputStreamImpl::Skip(int count) {
  ASSERT(count >= 0);
  drainLastSlice();

  // Could not skip more than buffer length.
  if (static_cast<uint64_t>(count) > buffer_->length()) {
    return false;
  }

  buffer_->drain(count);
  byte_count_ += count;
  return true;
}

void ZeroCopyInputStreamImpl::BackUp(int count) {
  ASSERT(count >= 0);
  ASSERT(uint64_t(count) <= position_);

  // Preconditions for BackUp:
  // - The last method called must have been Next().
  // - count must be less than or equal to the size of the last buffer returned by Next().
  // Due to preconditions above, it is safe to just adjust position_ and byte_count_ here, and
  // drain in Next().
  position_ -= count;
  byte_count_ -= count;
}
} // namespace Buffer
} // namespace Envoy
