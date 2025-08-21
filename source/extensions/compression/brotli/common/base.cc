#include "source/extensions/compression/brotli/common/base.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Common {

BrotliContext::BrotliContext(uint32_t chunk_size, uint32_t max_output_size)
    : max_output_size_{max_output_size}, chunk_size_{chunk_size},
      chunk_ptr_{std::make_unique<uint8_t[]>(chunk_size)}, next_out_{chunk_ptr_.get()},
      avail_out_{chunk_size} {}

void BrotliContext::updateOutput(Buffer::Instance& output_buffer) {
  if (avail_out_ == 0) {
    output_buffer.add(static_cast<void*>(chunk_ptr_.get()), chunk_size_);
    resetOut();
  }
}

void BrotliContext::finalizeOutput(Buffer::Instance& output_buffer) {
  const size_t n_output = chunk_size_ - avail_out_;
  if (n_output > 0) {
    output_buffer.add(static_cast<void*>(chunk_ptr_.get()), n_output);
  }
}

void BrotliContext::resetOut() {
  avail_out_ = chunk_size_;
  next_out_ = chunk_ptr_.get();
}

} // namespace Common
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
