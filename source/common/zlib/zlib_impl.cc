#include "common/zlib/zlib_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"

#include <iostream>

namespace Envoy {
namespace Zlib {

  Impl::Impl() {
    ZstreamPtr_->zalloc = Z_NULL;
    ZstreamPtr_->zfree = Z_NULL;
    ZstreamPtr_->opaque = Z_NULL;
    deflateInit2(ZstreamPtr_.get(), Z_BEST_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    buffer_ = std::unique_ptr<Envoy::Buffer::Instance>(new Buffer::OwnedImpl());
  }

  uint64_t Impl::getTotalIn() {
    return ZstreamPtr_->total_in;
  }

  uint64_t Impl::getTotalOut() {
    return ZstreamPtr_->total_out;
  }

  void Impl::endStream() {
    ASSERT(deflateEnd(ZstreamPtr_.get()) != Z_STREAM_ERROR);
  }

  Buffer::Instance& Impl::moveOut() {
    return *buffer_;
  }

  bool Impl::deflateData(Buffer::Instance& in) {
    Buffer::RawSlice in_slice{};
    Buffer::RawSlice out_slice{};
    
    buffer_->reserve(4096, &out_slice, 1);
    
    ZstreamPtr_->avail_out = out_slice.len_;
    ZstreamPtr_->next_out = static_cast<Bytef *>(out_slice.mem_);

    int result{};

    while (in.getRawSlices(&in_slice, 1)) {
      ZstreamPtr_->avail_in = in_slice.len_;
      ZstreamPtr_->next_in = static_cast<Bytef *>(in_slice.mem_);   
      result = deflate(ZstreamPtr_.get(), Z_NO_FLUSH);
      if (result != Z_OK) {
        std::cout << "no flush: " << result << std::endl;
        
      }

      in.drain(in_slice.len_);
    }; 

    result = deflate(ZstreamPtr_.get(), Z_SYNC_FLUSH);
    if (result != Z_OK) {
      std::cout << "sync flush: " << result << std::endl;
      
    }
    
    out_slice.len_ = out_slice.len_ - ZstreamPtr_->avail_out;
    buffer_->commit(&out_slice, 1);

    return true;
  }

} // namespace zlib 
} // namespace envoy