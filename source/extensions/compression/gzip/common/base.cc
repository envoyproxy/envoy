#include "source/extensions/compression/gzip/common/base.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Common {

Base::Base(uint64_t chunk_size, std::function<void(z_stream*)> zstream_deleter)
    : chunk_size_{chunk_size}, chunk_char_ptr_(new unsigned char[chunk_size]),
      zstream_ptr_(new z_stream(), zstream_deleter) {}

uint64_t Base::checksum() { return zstream_ptr_->adler; }

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
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
