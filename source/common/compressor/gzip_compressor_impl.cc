#include "common/compressor/gzip_compressor_impl.h"

namespace Envoy {
namespace Compressor {

  GzipCompressorImpl::GzipCompressorImpl() {
    ZlibPtr_ = std::unique_ptr<z_stream>(new z_stream());
  }

  // In the case of finish() not being explicitly called.
  // Leaving out this call will cause leaks.
  GzipCompressorImpl::~GzipCompressorImpl() {  
    finish();
  }

  uint64_t GzipCompressorImpl::getTotalIn() {
    return ZlibPtr_->total_in;
  }

  uint64_t GzipCompressorImpl::getTotalOut() {
    return ZlibPtr_->total_out;
  }

  bool GzipCompressorImpl::init() {
    ZlibPtr_->zalloc = Z_NULL;
    ZlibPtr_->zfree = Z_NULL;
    ZlibPtr_->opaque = Z_NULL;  

    return deflateInit2(ZlibPtr_.get(), Z_DEFAULT_COMPRESSION, Z_DEFLATED,
        window_bits_, memory_level_, Z_DEFAULT_STRATEGY) == Z_OK;
  }

  bool GzipCompressorImpl::finish() {
    return deflateEnd(ZlibPtr_.get()) != Z_STREAM_ERROR;
  }

  bool GzipCompressorImpl::compress(Buffer::Instance& in, Buffer::Instance& out) {
    if (!in.length()) {
      return true;
    }

    Buffer::RawSlice in_slice{};
    Buffer::RawSlice out_slice{};

    out.reserve(chunk_, &out_slice, 1);
    
    ZlibPtr_->avail_out = out_slice.len_;
    ZlibPtr_->next_out = static_cast<Bytef *>(out_slice.mem_);
    
    while (in.getRawSlices(&in_slice, 1)) {
      ZlibPtr_->avail_in = in_slice.len_;
      ZlibPtr_->next_in = static_cast<Bytef *>(in_slice.mem_);
     
      if (deflate(ZlibPtr_.get(), Z_NO_FLUSH) != Z_OK) {
        return false;
      }
     
      in.drain(in_slice.len_);
    }; 

    if (deflate(ZlibPtr_.get(), Z_SYNC_FLUSH) != Z_OK) {
      return false;
    }
   
    out_slice.len_ = out_slice.len_ - ZlibPtr_->avail_out;
    out.commit(&out_slice, 1);

    return true;
  }

} // namespace compressor 
} // namespace envoy