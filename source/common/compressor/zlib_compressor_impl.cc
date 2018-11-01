#include "common/compressor/zlib_compressor_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/stack_array.h"

namespace Envoy {
namespace Compressor {

ZlibCompressorImpl::ZlibCompressorImpl() : ZlibCompressorImpl(4096) {}

ZlibCompressorImpl::ZlibCompressorImpl(uint64_t chunk_size)
    : chunk_size_{chunk_size}, initialized_{false}, chunk_char_ptr_(new unsigned char[chunk_size]),
      zstream_ptr_(new z_stream(), [](z_stream* z) {
        deflateEnd(z);
        delete z;
      }) {
  zstream_ptr_->zalloc = Z_NULL;
  zstream_ptr_->zfree = Z_NULL;
  zstream_ptr_->opaque = Z_NULL;
  zstream_ptr_->avail_out = chunk_size_;
  zstream_ptr_->next_out = chunk_char_ptr_.get();
}

void ZlibCompressorImpl::init(CompressionLevel comp_level, CompressionStrategy comp_strategy,
                              int64_t window_bits, uint64_t memory_level = 8) {
  ASSERT(initialized_ == false);
  const int result = deflateInit2(zstream_ptr_.get(), static_cast<int64_t>(comp_level), Z_DEFLATED,
                                  window_bits, memory_level, static_cast<uint64_t>(comp_strategy));
  RELEASE_ASSERT(result >= 0, "");
  initialized_ = true;
}

uint64_t ZlibCompressorImpl::checksum() { return zstream_ptr_->adler; }

void ZlibCompressorImpl::compress(Buffer::Instance& buffer, State state) {
  const uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);

  for (const Buffer::RawSlice& input_slice : slices) {
    zstream_ptr_->avail_in = input_slice.len_;
    zstream_ptr_->next_in = static_cast<Bytef*>(input_slice.mem_);
    // Z_NO_FLUSH tells the compressor to take the data in and compresses it as much as possible
    // without flushing it out. However, if the data output is greater or equal to the allocated
    // chunk size, process() outputs it to the end of the buffer. This is fine, since at the next
    // step, the buffer is drained from the beginning of the buffer by the size of input.
    process(buffer, Z_NO_FLUSH);
    buffer.drain(input_slice.len_);
  }

  process(buffer, state == State::Finish ? Z_FINISH : Z_SYNC_FLUSH);
}

bool ZlibCompressorImpl::deflateNext(int64_t flush_state) {
  const int result = deflate(zstream_ptr_.get(), flush_state);
  switch (flush_state) {
  case Z_FINISH:
    if (result != Z_OK && result != Z_BUF_ERROR) {
      RELEASE_ASSERT(result == Z_STREAM_END, "");
      return false;
    }
    FALLTHRU;
  default:
    if (result == Z_BUF_ERROR && zstream_ptr_->avail_in == 0) {
      return false; // This means that zlib needs more input, so stop here.
    }
    RELEASE_ASSERT(result == Z_OK, "");
  }

  return true;
}

void ZlibCompressorImpl::process(Buffer::Instance& output_buffer, int64_t flush_state) {
  while (deflateNext(flush_state)) {
    if (zstream_ptr_->avail_out == 0) {
      updateOutput(output_buffer);
    }
  }

  if (flush_state == Z_SYNC_FLUSH || flush_state == Z_FINISH) {
    updateOutput(output_buffer);
  }
}

void ZlibCompressorImpl::updateOutput(Buffer::Instance& output_buffer) {
  const uint64_t n_output = chunk_size_ - zstream_ptr_->avail_out;
  if (n_output > 0) {
    output_buffer.add(static_cast<void*>(chunk_char_ptr_.get()), n_output);
  }
  chunk_char_ptr_.reset(new unsigned char[chunk_size_]);
  zstream_ptr_->avail_out = chunk_size_;
  zstream_ptr_->next_out = chunk_char_ptr_.get();
}

} // namespace Compressor
} // namespace Envoy
