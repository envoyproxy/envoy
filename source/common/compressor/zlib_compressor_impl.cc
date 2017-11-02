#include "common/compressor/zlib_compressor_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Compressor {

ZlibCompressorImpl::ZlibCompressorImpl()
    : zstream_ptr_(new z_stream(), [](z_stream* z) {
        deflateEnd(z);
        delete z;
      }) {
  zstream_ptr_->zalloc = Z_NULL;
  zstream_ptr_->zfree = Z_NULL;
  zstream_ptr_->opaque = Z_NULL;
}

void ZlibCompressorImpl::init(CompressionLevel comp_level, CompressionStrategy comp_strategy,
                              int8_t window_bits, uint8_t memory_level = 8) {
  ASSERT(initialized_ == false);
  const int result = deflateInit2(zstream_ptr_.get(), static_cast<int>(comp_level), Z_DEFLATED,
                                  static_cast<int>(window_bits), static_cast<int>(memory_level),
                                  static_cast<int>(comp_strategy));
  RELEASE_ASSERT(result >= 0);
  initialized_ = true;
}

void ZlibCompressorImpl::setChunk(uint64_t chunk) { chunk_ = chunk; }

void ZlibCompressorImpl::finish(Buffer::Instance& output_buffer) {
  process(output_buffer, Z_SYNC_FLUSH);
  commit(output_buffer);
}

void ZlibCompressorImpl::compress(const Buffer::Instance& input_buffer,
                                  Buffer::Instance& output_buffer) {
  if (zstream_ptr_->total_out == 0) {
    reserve(output_buffer);
  }

  const uint64_t num_slices = input_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  input_buffer.getRawSlices(slices, num_slices);

  for (const Buffer::RawSlice& input_slice : slices) {
    zstream_ptr_->avail_in = input_slice.len_;
    zstream_ptr_->next_in = static_cast<Bytef*>(input_slice.mem_);
    process(output_buffer, Z_NO_FLUSH);
  }
}

void ZlibCompressorImpl::process(Buffer::Instance& output_buffer, uint8_t flush_state) {
  do {
    if (zstream_ptr_->avail_out == 0) {
      commit(output_buffer);
      reserve(output_buffer);
    }
    const int result = deflate(zstream_ptr_.get(), static_cast<int>(flush_state));
    RELEASE_ASSERT(result >= 0);
  } while (zstream_ptr_->avail_out == 0);
}

void ZlibCompressorImpl::reserve(Buffer::Instance& output_buffer) {
  output_slice_ptr_.reset(new Buffer::RawSlice());
  output_buffer.reserve(chunk_, output_slice_ptr_.get(), 1);
  zstream_ptr_->avail_out = output_slice_ptr_->len_;
  zstream_ptr_->next_out = static_cast<Bytef*>(output_slice_ptr_->mem_);
}

void ZlibCompressorImpl::commit(Buffer::Instance& output_buffer) {
  output_slice_ptr_->len_ = output_slice_ptr_->len_ - zstream_ptr_->avail_out;
  output_buffer.commit(output_slice_ptr_.get(), 1);
}

} // namespace Compressor
} // namespace Envoy
