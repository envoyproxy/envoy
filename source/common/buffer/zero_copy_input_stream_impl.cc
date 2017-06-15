#include "common/common/assert.h"
#include "common/buffer/zero_copy_input_stream_impl.h"

namespace Envoy {
namespace Buffer {

ZeroCopyInputStreamImpl::ZeroCopyInputStreamImpl(Buffer::Instance& buffer) {
  move(buffer);
  finish();
}

void ZeroCopyInputStreamImpl::move(Buffer::Instance &instance) {
  ASSERT(!finished_);

  buffer_.move(instance);
}

bool ZeroCopyInputStreamImpl::Next(const void **data, int *size){
  if (position_ != 0) {
    buffer_.drain(position_);
    position_ = 0;
  }

  Buffer::RawSlice slice;
  uint64_t num_slices = buffer_.getRawSlices(&slice, 1);

  if (num_slices > 0) {
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

bool ZeroCopyInputStreamImpl::Skip(int) {
  NOT_IMPLEMENTED;
}

void ZeroCopyInputStreamImpl::BackUp(int count) {
  ASSERT(count > 0);
  ASSERT(uint64_t(count) < position_);

  position_ -= count;
  byte_count_ -= count;
}

}
}