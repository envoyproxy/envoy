#include "common/compressor/zlib_compressor_impl.h"

namespace Envoy {
namespace Compressor {

ZlibCompressorImpl::ZlibCompressorImpl() : zlib_ptr_(new z_stream()) {}

ZlibCompressorImpl::~ZlibCompressorImpl() {
  if (is_deflate_) {
    deflateEnd(zlib_ptr_.get());
  } else {
    inflateEnd(zlib_ptr_.get());
  }
}

void ZlibCompressorImpl::setChunk(uint64_t chunk) { chunk_ = chunk; }

bool ZlibCompressorImpl::init(CompressionLevel comp_level, CompressionStrategy comp_strategy,
                              int window_bits, uint memory_level) {
  zlib_ptr_->zalloc = Z_NULL;
  zlib_ptr_->zfree = Z_NULL;
  zlib_ptr_->opaque = Z_NULL;

  int result = deflateInit2(zlib_ptr_.get(), comp_level, Z_DEFLATED, window_bits, memory_level,
                            comp_strategy);

  return result == Z_OK;
}

bool ZlibCompressorImpl::init(int window_bits) {
  is_deflate_ = false;

  zlib_ptr_->zalloc = Z_NULL;
  zlib_ptr_->zfree = Z_NULL;
  zlib_ptr_->opaque = Z_NULL;

  int result = inflateInit2(zlib_ptr_.get(), window_bits);

  return result == Z_OK;
}

bool ZlibCompressorImpl::finish() {
  int result{};

  if (is_deflate_) {
    result = deflateReset(zlib_ptr_.get());
  } else {
    result = inflateReset(zlib_ptr_.get());
  }

  return result != Z_STREAM_ERROR;
}

bool ZlibCompressorImpl::compress(const Buffer::Instance& in, Buffer::Instance& out) {
  return process(in, out, &deflate);
}

bool ZlibCompressorImpl::decompress(const Buffer::Instance& in, Buffer::Instance& out) {
  return process(in, out, &inflate);
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

  uint64_t num_slices = in.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  in.getRawSlices(slices, num_slices);
  uint64_t t_bytes{0};

  for (Buffer::RawSlice& slice : slices) {
    zlib_ptr_->avail_in = slice.len_;
    zlib_ptr_->next_in = static_cast<Bytef*>(slice.mem_);
    t_bytes += zlib_ptr_->avail_in;

    if (zlib_func_ptr(zlib_ptr_.get(), t_bytes < in.length() ? Z_NO_FLUSH : Z_SYNC_FLUSH) != Z_OK) {
      return false;
    }
  }

  out_slice.len_ = out_slice.len_ - zlib_ptr_->avail_out;
  out.commit(&out_slice, 1);

  return true;
}

} // namespace Compressor
} // namespace Envoy
