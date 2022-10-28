#include "source/extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

#include <zlib.h>

#include <memory>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Decompressor {

ZlibDecompressorImpl::ZlibDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix,
                                           uint64_t chunk_size, uint64_t max_inflate_ratio)
    : Common::Base(chunk_size,
                   [](z_stream* z) {
                     inflateEnd(z);
                     delete z;
                   }),
      stats_(generateStats(stats_prefix, scope)), max_inflate_ratio_(max_inflate_ratio) {
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

void ZlibDecompressorImpl::decompress(const Buffer::Instance& input_buffer,
                                      Buffer::Instance& output_buffer) {
  uint64_t limit = max_inflate_ratio_ * input_buffer.length();

  for (const Buffer::RawSlice& input_slice : input_buffer.getRawSlices()) {
    zstream_ptr_->avail_in = input_slice.len_;
    zstream_ptr_->next_in = static_cast<Bytef*>(input_slice.mem_);
    while (inflateNext()) {
      if (zstream_ptr_->avail_out == 0) {
        updateOutput(output_buffer);
      }

      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.enable_compression_bomb_protection") &&
          (output_buffer.length() > limit)) {
        stats_.zlib_data_error_.inc();
        ENVOY_LOG(trace,
                  "excessive decompression ratio detected: output "
                  "size {} for input size {}",
                  output_buffer.length(), input_buffer.length());
        return;
      }
    }
  }

  // Flush z_stream and reset its buffer. Otherwise the stale content of the buffer
  // will pollute output upon the next call to decompress().
  updateOutput(output_buffer);
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

  if (result < 0) {
    decompression_error_ = result;
    ENVOY_LOG(trace,
              "zlib decompression error: {}, msg: {}. Error codes are defined in "
              "https://www.zlib.net/manual.html",
              result, zstream_ptr_->msg);
    chargeErrorStats(result);
    return false;
  }

  return true;
}

void ZlibDecompressorImpl::chargeErrorStats(const int result) {
  switch (result) {
  case Z_ERRNO:
    stats_.zlib_errno_.inc();
    break;
  case Z_STREAM_ERROR:
    stats_.zlib_stream_error_.inc();
    break;
  case Z_DATA_ERROR:
    stats_.zlib_data_error_.inc();
    break;
  case Z_MEM_ERROR:
    stats_.zlib_mem_error_.inc();
    break;
  case Z_BUF_ERROR:
    stats_.zlib_buf_error_.inc();
    break;
  case Z_VERSION_ERROR:
    stats_.zlib_version_error_.inc();
    break;
  }
}

} // namespace Decompressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
