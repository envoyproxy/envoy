#include "source/extensions/compression/zstd/compressor/zstd_compressor_impl.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Compressor {

ZstdCompressorImpl::ZstdCompressorImpl(const uint32_t compression_level, const bool enable_checksum,
                                       const uint32_t strategy,
                                       const ZstdCDictManagerPtr& cdict_manager,
                                       const uint32_t chunk_size)
    : Common::Base(chunk_size), cctx_(ZSTD_createCCtx(), &ZSTD_freeCCtx),
      cdict_manager_(cdict_manager), compression_level_(compression_level) {
  size_t result;
  result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_checksumFlag, enable_checksum);
  RELEASE_ASSERT(!ZSTD_isError(result), "");

  result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_strategy, strategy);
  RELEASE_ASSERT(!ZSTD_isError(result), "");

  if (cdict_manager_) {
    ZSTD_CDict* cdict = cdict_manager_->getFirstDictionary();
    result = ZSTD_CCtx_refCDict(cctx_.get(), cdict);
  } else {
    result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_compressionLevel, compression_level_);
  }
  RELEASE_ASSERT(!ZSTD_isError(result), "");
}

void ZstdCompressorImpl::compress(Buffer::Instance& buffer,
                                  Envoy::Compression::Compressor::State state) {
  Buffer::OwnedImpl accumulation_buffer;
  for (const Buffer::RawSlice& input_slice : buffer.getRawSlices()) {
    if (input_slice.len_ > 0) {
      setInput(input_slice);
      process(accumulation_buffer, ZSTD_e_continue);
      buffer.drain(input_slice.len_);
    }
  }

  ASSERT(buffer.length() == 0);
  buffer.move(accumulation_buffer);

  if (state == Envoy::Compression::Compressor::State::Finish) {
    process(buffer, ZSTD_e_end);
  }
}

void ZstdCompressorImpl::process(Buffer::Instance& output_buffer, const ZSTD_EndDirective mode) {
  bool finished;
  do {
    size_t const remaining = ZSTD_compressStream2(cctx_.get(), &output_, &input_, mode);
    getOutput(output_buffer);
    // If we're on the last chunk we're finished when zstd returns 0,
    // which means its consumed all the input AND finished the frame.
    // Otherwise, we're finished when we've consumed all the input.
    finished = (ZSTD_e_end == mode) ? (remaining == 0) : (input_.pos == input_.size);
  } while (!finished);
}

} // namespace Compressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
