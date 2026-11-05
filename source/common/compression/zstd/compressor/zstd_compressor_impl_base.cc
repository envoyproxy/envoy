#include "source/common/compression/zstd/compressor/zstd_compressor_impl_base.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Compression {
namespace Zstd {
namespace Compressor {

ZstdCompressorImplBase::ZstdCompressorImplBase(uint32_t compression_level, bool enable_checksum,
                                               uint32_t strategy, uint32_t chunk_size)
    : Common::Base(chunk_size), cctx_(ZSTD_createCCtx(), &ZSTD_freeCCtx),
      compression_level_(compression_level),
      flush_on_flush_state_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.zstd_compressor_flush")) {
  size_t result;
  result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_checksumFlag, enable_checksum);
  RELEASE_ASSERT(!ZSTD_isError(result), "");

  result = ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_strategy, strategy);
  RELEASE_ASSERT(!ZSTD_isError(result), "");
}

void ZstdCompressorImplBase::compress(Buffer::Instance& buffer,
                                      Envoy::Compression::Compressor::State state) {
  compressPreprocess(buffer, state);

  Buffer::OwnedImpl accumulation_buffer;
  for (const Buffer::RawSlice& input_slice : buffer.getRawSlices()) {
    if (input_slice.len_ > 0) {
      compressProcess(buffer, input_slice, accumulation_buffer);
      buffer.drain(input_slice.len_);
    }
  }

  compressPostprocess(accumulation_buffer);

  ASSERT(buffer.length() == 0);
  buffer.move(accumulation_buffer);

  if (state == Envoy::Compression::Compressor::State::Finish) {
    process(buffer, ZSTD_e_end);
  } else if (flush_on_flush_state_) {
    // Write out everything zstd has buffered so far, so that data the caller flushes (e.g. a
    // message on a long-lived stream) is not held back until a block fills up or the stream ends.
    process(buffer, ZSTD_e_flush);
  }
}

void ZstdCompressorImplBase::process(Buffer::Instance& output_buffer, ZSTD_EndDirective mode) {
  bool finished;
  do {
    const size_t remaining = ZSTD_compressStream2(cctx_.get(), &output_, &input_, mode);
    getOutput(output_buffer);
    // When flushing or ending the frame we're finished when zstd returns 0, which means it has
    // consumed all the input AND written out everything it buffered (and, for ZSTD_e_end,
    // finished the frame). Otherwise, we're finished when we've consumed all the input.
    finished = (ZSTD_e_continue == mode) ? (input_.pos == input_.size) : (remaining == 0);
  } while (!finished);
}

} // namespace Compressor
} // namespace Zstd
} // namespace Compression
} // namespace Envoy
