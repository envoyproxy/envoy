#include "source/common/compression/zstd/compressor/zstd_compressor_impl_base.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Compression {
namespace Zstd {
namespace Compressor {

ZstdCompressorImplBase::ZstdCompressorImplBase(uint32_t compression_level, bool enable_checksum,
                                               uint32_t strategy, uint32_t chunk_size)
    : Common::Base(chunk_size), cctx_(ZSTD_createCCtx(), &ZSTD_freeCCtx),
      compression_level_(compression_level) {
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
  }
}

void ZstdCompressorImplBase::process(Buffer::Instance& output_buffer, ZSTD_EndDirective mode) {
  bool finished;
  do {
    const size_t remaining = ZSTD_compressStream2(cctx_.get(), &output_, &input_, mode);
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
} // namespace Envoy
