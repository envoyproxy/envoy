#include "source/common/compression/zstd/common/base.h"

namespace Envoy {
namespace Compression {
namespace Zstd {
namespace Common {

Base::Base(uint32_t chunk_size)
    : chunk_ptr_{std::make_unique<uint8_t[]>(chunk_size)}, output_{chunk_ptr_.get(), chunk_size, 0},
      input_{nullptr, 0, 0} {}

void Base::setInput(const Buffer::RawSlice& input_slice) {
  input_.src = static_cast<uint8_t*>(input_slice.mem_);
  input_.pos = 0;
  input_.size = input_slice.len_;
}

void Base::getOutput(Buffer::Instance& output_buffer) {
  if (output_.pos > 0) {
    output_buffer.add(static_cast<void*>(chunk_ptr_.get()), output_.pos);
    output_.pos = 0;
    output_.dst = chunk_ptr_.get();
  }
}

} // namespace Common
} // namespace Zstd
} // namespace Compression
} // namespace Envoy
