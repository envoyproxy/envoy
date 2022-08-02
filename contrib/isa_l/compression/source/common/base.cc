#include "contrib/isa_l/compression/source/common/base.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Igzip {
namespace Common {

Base::Base(uint64_t chunk_size, std::function<void(isal_zstream*)> zstream_deleter)
    : chunk_size_{chunk_size}, chunk_char_ptr_(new unsigned char[chunk_size]),
      zstream_ptr_(new isal_zstream(), zstream_deleter) {}

uint32_t Base::checksum() { return zstream_ptr_->internal_state.crc; }

void Base::updateOutput(Buffer::Instance& output_buffer) {
  const uint64_t n_output = chunk_size_ - zstream_ptr_->avail_out;
  if (n_output == 0) {
    return;
  }

  output_buffer.add(static_cast<void*>(chunk_char_ptr_.get()), n_output);
  zstream_ptr_->avail_out = chunk_size_;
  zstream_ptr_->next_out = chunk_char_ptr_.get();
}

} // namespace Common
} // namespace Igzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
