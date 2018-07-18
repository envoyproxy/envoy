#include "common/decompressor/zlib_decompressor_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Decompressor {

ZlibDecompressorImpl::ZlibDecompressorImpl() : ZlibDecompressorImpl(4096) {}

ZlibDecompressorImpl::ZlibDecompressorImpl(uint64_t chunk_size)
    : chunk_size_{chunk_size}, initialized_{false}, chunk_char_ptr_(new unsigned char[chunk_size]),
      zstream_ptr_(new z_stream(), [](z_stream* z) {
        inflateEnd(z);
        delete z;
      }) {
  zstream_ptr_->zalloc = Z_NULL;
  zstream_ptr_->zfree = Z_NULL;
  zstream_ptr_->opaque = Z_NULL;
  zstream_ptr_->avail_out = chunk_size_;
  zstream_ptr_->next_out = chunk_char_ptr_.get();
}

void ZlibDecompressorImpl::init(int64_t window_bits) {
  ASSERT(initialized_ == false);
  const int result = inflateInit2(zstream_ptr_.get(), window_bits);
  RELEASE_ASSERT(result >= 0, "");
  initialized_ = true;
}

uint64_t ZlibDecompressorImpl::checksum() { return zstream_ptr_->adler; }

void ZlibDecompressorImpl::decompress(const Buffer::Instance& input_buffer,
                                      Buffer::Instance& output_buffer) {
  const uint64_t num_slices = input_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  input_buffer.getRawSlices(slices, num_slices);

  for (const Buffer::RawSlice& input_slice : slices) {
    zstream_ptr_->avail_in = input_slice.len_;
    zstream_ptr_->next_in = static_cast<Bytef*>(input_slice.mem_);
    while (inflateNext()) {
      if (zstream_ptr_->avail_out == 0) {
        output_buffer.add(static_cast<void*>(chunk_char_ptr_.get()),
                          chunk_size_ - zstream_ptr_->avail_out);
        chunk_char_ptr_.reset(new unsigned char[chunk_size_]);
        zstream_ptr_->avail_out = chunk_size_;
        zstream_ptr_->next_out = chunk_char_ptr_.get();
      }
    }
  }

  const uint64_t n_output{chunk_size_ - zstream_ptr_->avail_out};
  if (n_output > 0) {
    output_buffer.add(static_cast<void*>(chunk_char_ptr_.get()), n_output);
  }
}

bool ZlibDecompressorImpl::inflateNext() {
  const int result = inflate(zstream_ptr_.get(), Z_NO_FLUSH);
  if (result == Z_STREAM_END) {
    // Z_FINISH informs inflate to not maintain a sliding window if the stream completes, which
    // reduces inflate's memory footprint. Ref: https://www.zlib.net/manual.html.
    inflate(zstream_ptr_.get(), Z_FINISH);
    return false;
  }

  if (result == Z_BUF_ERROR && zstream_ptr_->avail_in == 0) {
    return false; // This means that zlib needs more input, so stop here.
  }

  RELEASE_ASSERT(result == Z_OK, "");
  return true;
}

} // namespace Decompressor
} // namespace Envoy
