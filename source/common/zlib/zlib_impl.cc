#include "common/zlib/zlib_impl.h"
#include "common/buffer/buffer_impl.h"

#include <iostream>

namespace Envoy {
namespace Zlib {

  Impl::Impl() {
    ZstreamPtr_ = std::unique_ptr<z_stream>(new z_stream());
    ZstreamPtr_->zalloc = Z_NULL;
    ZstreamPtr_->zfree = Z_NULL;
    ZstreamPtr_->opaque = Z_NULL;
    buffer_ = std::unique_ptr<Envoy::Buffer::Instance>(new Buffer::OwnedImpl());
  }

  uint64_t Impl::getTotalIn() {
    return total_in_;
  }

  uint64_t Impl::getTotalOut() {
    return total_out_;
  }

  bool Impl::deflateData(Buffer::Instance& data) {
    if (deflateInit(ZstreamPtr_.get(), Z_BEST_COMPRESSION) != Z_OK) {
      return false;
    }

    Buffer::RawSlice in_slice{};
    Buffer::RawSlice out_slice{};
  
    uint data_len{};
    uint num_bytes_read{};
    uint num_bytes_write{};
    uint num_slices{};
    uint flush_state{};
    uint result{};
  
    while (data.length() > 0) {
      num_slices = data.getRawSlices(&in_slice, 1);
      if (num_slices) {
        ZstreamPtr_->avail_in = in_slice.len_;
        ZstreamPtr_->next_in = static_cast<Bytef *>(in_slice.mem_);
      }
  
      data_len = data.length();
      if (data_len - ZstreamPtr_->avail_in == 0) {
        flush_state = Z_FINISH;
      } else {
        flush_state = Z_SYNC_FLUSH;
      }
  
      buffer_->reserve(data_len, &out_slice, 1);
      ZstreamPtr_->avail_out = out_slice.len_;
      ZstreamPtr_->next_out = static_cast<Bytef *>(out_slice.mem_);
  
      result = deflate(ZstreamPtr_.get(), flush_state);
      if (result != Z_OK && result != Z_STREAM_END) {
        return false;
      }
  
      num_bytes_read = in_slice.len_ - ZstreamPtr_->avail_in;
      num_bytes_write = out_slice.len_ - ZstreamPtr_->avail_out;
      data.drain(num_bytes_read);
      out_slice.len_ = num_bytes_write;
      buffer_->commit(&out_slice, 1);
      total_out_ += num_bytes_write;
      total_in_ += num_bytes_read;
    };
  
    if (deflateEnd(ZstreamPtr_.get()) != Z_OK) {
      return false;
    }
  
    data.move(*buffer_);
    return true;
  }

} // namespace zlib 
} // namespace envoy