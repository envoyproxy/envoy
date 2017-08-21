#include "common/compressor/compressor_impl.h"

namespace Envoy {
namespace Compressor {

  Impl::Impl() {}
  Impl::~Impl() {
    if (!is_finished_) {
      finish();
    }
  }

  uint64_t Impl::getTotalIn() {
    return zstream.total_in;
  }

  uint64_t Impl::getTotalOut() {
    return zstream.total_out;
  }

  bool Impl::init(CompressionLevel compression_level) {
    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;

    const int z_window_bits_{15 | 16};
    const int z_memory_level_{8};

    int result = deflateInit2(&zstream, compression_level, Z_DEFLATED,
        z_window_bits_, z_memory_level_, Z_DEFAULT_STRATEGY);
    
    return result == Z_OK;
  }

  bool Impl::finish() {
    int result = deflateEnd(&zstream);
    is_finished_ = true;
    return result != Z_STREAM_ERROR;
  }

  bool Impl::compress(Buffer::Instance& in, Buffer::Instance& out) {
    if (!in.length()) {
      return true;
    }
    Buffer::RawSlice in_slice{};
    Buffer::RawSlice out_slice{};

    out.reserve(4096, &out_slice, 1);
    
    zstream.avail_out = out_slice.len_;
    zstream.next_out = static_cast<Bytef *>(out_slice.mem_);
    
    while (in.getRawSlices(&in_slice, 1)) {
      zstream.avail_in = in_slice.len_;
      zstream.next_in = static_cast<Bytef *>(in_slice.mem_);
      if (deflate(&zstream, Z_NO_FLUSH) != Z_OK) {
        return false;
      }
      in.drain(in_slice.len_);
    }; 

    if (deflate(&zstream, Z_SYNC_FLUSH) != Z_OK) {
      return false;
    }
    
    out_slice.len_ = out_slice.len_ - zstream.avail_out;
    out.commit(&out_slice, 1);

    return true;
  }

} // namespace compressor 
} // namespace envoy