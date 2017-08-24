#include "common/compressor/zlib_compressor_impl.h"

namespace Envoy {
namespace Compressor {

ZlibCompressorImpl::ZlibCompressorImpl() { ZlibPtr_ = std::unique_ptr<z_stream>(new z_stream()); }

// By not calling deflateEnd() will cause leaks.
ZlibCompressorImpl::~ZlibCompressorImpl() { deflateEnd(ZlibPtr_.get()); }

uint64_t ZlibCompressorImpl::getTotalIn() { return ZlibPtr_->total_in; }

uint64_t ZlibCompressorImpl::getTotalOut() { return ZlibPtr_->total_out; }

void ZlibCompressorImpl::setMemoryLevel(uint mem_level) { memory_level_ = mem_level; }

void ZlibCompressorImpl::setWindowBits(int win_bits) { window_bits_ = win_bits; }

void ZlibCompressorImpl::setChunk(uint64_t chunk) { chunk_ = chunk; }

bool ZlibCompressorImpl::init(CompressionLevel level, CompressionStrategy strategy) {
  ZlibPtr_->zalloc = Z_NULL;
  ZlibPtr_->zfree = Z_NULL;
  ZlibPtr_->opaque = Z_NULL;

  int result =
      deflateInit2(ZlibPtr_.get(), level, Z_DEFLATED, window_bits_, memory_level_, strategy);

  return result == Z_OK;
}

bool ZlibCompressorImpl::finish() {
  int result = deflateEnd(ZlibPtr_.get());

  return result != Z_STREAM_ERROR;
}

bool ZlibCompressorImpl::compress(Buffer::Instance& in, Buffer::Instance& out) {
  if (!in.length()) {
    return true;
  }

  Buffer::RawSlice in_slice{};
  Buffer::RawSlice out_slice{};

  out.reserve(chunk_, &out_slice, 1);

  ZlibPtr_->avail_out = out_slice.len_;
  ZlibPtr_->next_out = static_cast<Bytef*>(out_slice.mem_);

  while (in.getRawSlices(&in_slice, 1)) {
    ZlibPtr_->avail_in = in_slice.len_;
    ZlibPtr_->next_in = static_cast<Bytef*>(in_slice.mem_);

    if (deflate(ZlibPtr_.get(), Z_NO_FLUSH) != Z_OK) {
      return false;
    }

    in.drain(in_slice.len_ - ZlibPtr_->avail_in);
  };

  if (deflate(ZlibPtr_.get(), Z_SYNC_FLUSH) != Z_OK) {
    return false;
  }

  out_slice.len_ = out_slice.len_ - ZlibPtr_->avail_out;
  out.commit(&out_slice, 1);

  return true;
}

bool ZlibCompressorImpl::decompress(Buffer::Instance&, Buffer::Instance&) {
  NOT_IMPLEMENTED;
  return true;
}

} // namespace Compressor
} // namespace Envoy
