#include "contrib/isa_l/compression/source/compressor/igzip_compressor_impl.h"

#include <memory>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"

#include "absl/container/fixed_array.h"

#include "isa-l.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Igzip {
namespace Compressor {

IgzipCompressorImpl::IgzipCompressorImpl() : IgzipCompressorImpl(4096) {}

IgzipCompressorImpl::IgzipCompressorImpl(uint64_t chunk_size)
    : Common::Base(chunk_size, [](isal_zstream* z) {
        free(z->level_buf);
        delete z;
      }) {
}

void IgzipCompressorImpl::init(CompressionLevel comp_level,
                              int64_t window_bits) {
  ASSERT(initialized_ == false);
  (void)window_bits;
  isal_deflate_init(zstream_ptr_.get());
  zstream_ptr_->avail_out = chunk_size_;
  zstream_ptr_->next_out = chunk_char_ptr_.get();
  zstream_ptr_->gzip_flag = IGZIP_GZIP;

  if (comp_level == CompressionLevel::Level1) {
		zstream_ptr_->level = 1;
		zstream_ptr_->level_buf = static_cast<uint8_t *>(malloc(ISAL_DEF_LVL1_DEFAULT));
		zstream_ptr_->level_buf_size = ISAL_DEF_LVL1_DEFAULT;
	}else if (comp_level == CompressionLevel::Level2) {
		zstream_ptr_->level = 2;
		zstream_ptr_->level_buf = static_cast<uint8_t *>(malloc(ISAL_DEF_LVL2_DEFAULT));
		zstream_ptr_->level_buf_size = ISAL_DEF_LVL2_DEFAULT;		
  }else{
		zstream_ptr_->level = 3;
		zstream_ptr_->level_buf = static_cast<uint8_t *>(malloc(ISAL_DEF_LVL3_DEFAULT));
		zstream_ptr_->level_buf_size = ISAL_DEF_LVL3_DEFAULT;		
	}

  initialized_ = true;
}

void IgzipCompressorImpl::compress(Buffer::Instance& buffer,
                                  Envoy::Compression::Compressor::State state) {
  for (const Buffer::RawSlice& input_slice : buffer.getRawSlices()) {
    zstream_ptr_->avail_in = input_slice.len_;
    zstream_ptr_->next_in = static_cast<uint8_t*>(input_slice.mem_);
    process(buffer, NO_FLUSH);
    buffer.drain(input_slice.len_);
  }
  
  process(buffer, state == Envoy::Compression::Compressor::State::Finish ? FULL_FLUSH : SYNC_FLUSH);
}

bool IgzipCompressorImpl::deflateNext() {
  const int result = isal_deflate(zstream_ptr_.get());

  RELEASE_ASSERT(result == COMP_OK, "");

  return zstream_ptr_->avail_out == 0;
}

void IgzipCompressorImpl::process(Buffer::Instance& output_buffer, int64_t flush_state) {
  zstream_ptr_->end_of_stream = (flush_state == FULL_FLUSH);
  while (deflateNext()) {
    if (zstream_ptr_->avail_out == 0) {
      updateOutput(output_buffer);
    }
  }
  RELEASE_ASSERT(zstream_ptr_->avail_in == 0, "");

  if (flush_state == SYNC_FLUSH || flush_state == FULL_FLUSH) {
    updateOutput(output_buffer);
  }
}

} // namespace Compressor
} // namespace Igzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
