#include "common/compressor/zlib_compressor_impl.h"

#include <iostream>

namespace Envoy {
namespace Compressor {

ZlibCompressorImpl::ZlibCompressorImpl() : zlib_ptr_(new z_stream()) {
  zlib_ptr_->zalloc = Z_NULL;
  zlib_ptr_->zfree = Z_NULL;
  zlib_ptr_->opaque = Z_NULL;
}

ZlibCompressorImpl::~ZlibCompressorImpl() {
  // deflateEnd/inflateEnd must be called, otherwise it will cause memory leaks.
  if (end_func_ptr_) {
    end_func_ptr_(zlib_ptr_.get());
  }
}

void ZlibCompressorImpl::setChunk(uint64_t chunk) { chunk_ = chunk; }

bool ZlibCompressorImpl::init(CompressionLevel comp_level, CompressionStrategy comp_strategy,
                              int window_bits, uint memory_level) {
  // prevents deflateInit2() of being called multiple times, which would cause memory leaks.
  if (end_func_ptr_) {
    return true;
  }

  if (deflateInit2(zlib_ptr_.get(), comp_level, Z_DEFLATED, window_bits, memory_level,
                   comp_strategy) != Z_OK) {
    return false;
  }

  end_func_ptr_ = &deflateEnd;
  reset_func_ptr_ = &deflateReset;

  return true;
}

bool ZlibCompressorImpl::init(int window_bits) {
  // prevents inflateInit2() of being called multiple times, which would cause memory leaks.
  if (end_func_ptr_) {
    return true;
  }

  if (inflateInit2(zlib_ptr_.get(), window_bits) != Z_OK) {
    return false;
  }

  end_func_ptr_ = &inflateEnd;
  reset_func_ptr_ = &inflateReset;

  return true;
}

bool ZlibCompressorImpl::reset() {
  if (reset_func_ptr_) {
    return reset_func_ptr_(zlib_ptr_.get()) != Z_STREAM_ERROR;
  }
  return true;
}

bool ZlibCompressorImpl::compress(const Buffer::Instance& in, Buffer::Instance& out) {
  if (end_func_ptr_) {
    return process(in, out, &deflate);
  }
  return true;
}

bool ZlibCompressorImpl::decompress(const Buffer::Instance& in, Buffer::Instance& out) {
  if (end_func_ptr_) {
    return process(in, out, &inflate);
  }
  return true;
}

bool ZlibCompressorImpl::process(const Buffer::Instance& in, Buffer::Instance& out,
                                 int (*zlib_func_ptr)(z_stream*, int)) {
  if (!in.length()) {
    return true;
  }

  Buffer::RawSlice out_slice{};
  out.reserve(chunk_, &out_slice, 1);

  zlib_ptr_->avail_out = out_slice.len_;
  zlib_ptr_->next_out = static_cast<Bytef*>(out_slice.mem_);

  const uint64_t num_slices = in.getRawSlices(nullptr, 0);
  Buffer::RawSlice in_slices[num_slices];
  in.getRawSlices(in_slices, num_slices);
  uint64_t byte_count{};

  for (Buffer::RawSlice& slice : in_slices) {
    zlib_ptr_->avail_in = slice.len_;
    zlib_ptr_->next_in = static_cast<Bytef*>(slice.mem_);
    byte_count += zlib_ptr_->avail_in;
    if (zlib_func_ptr(zlib_ptr_.get(), byte_count < in.length() ? Z_NO_FLUSH : Z_SYNC_FLUSH) !=
        Z_OK) {
      return false;
    }
  }

  out_slice.len_ = out_slice.len_ - zlib_ptr_->avail_out;
  out.commit(&out_slice, 1);

  return true;
}

} // namespace Compressor
} // namespace Envoy
