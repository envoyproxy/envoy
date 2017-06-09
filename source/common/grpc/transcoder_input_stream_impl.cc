#include "common/grpc/transcoder_input_stream_impl.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Grpc {

void TranscoderInputStreamImpl::Move(Buffer::Instance& instance) {
  if (!finished_) {
    buffer_.move(instance);
  }
}

bool TranscoderInputStreamImpl::Next(const void** data, int* size) {
  if (position_ != 0) {
    buffer_.drain(position_);
    position_ = 0;
  }

  Buffer::RawSlice slice;
  uint64_t num_slices = buffer_.getRawSlices(&slice, 1);

  if (num_slices) {
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

bool TranscoderInputStreamImpl::Skip(int) { NOT_IMPLEMENTED; }

void TranscoderInputStreamImpl::BackUp(int count) {
  GOOGLE_CHECK_GE(count, 0);
  GOOGLE_CHECK_LE(count, position_);

  position_ -= count;
  byte_count_ -= count;
}

int64_t TranscoderInputStreamImpl::BytesAvailable() const { return buffer_.length() - position_; }

} // namespace Grpc
} // namespace Envoy
