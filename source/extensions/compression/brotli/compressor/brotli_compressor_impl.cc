#include "source/extensions/compression/brotli/compressor/brotli_compressor_impl.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Compressor {

BrotliCompressorImpl::BrotliCompressorImpl(const uint32_t quality, const uint32_t window_bits,
                                           const uint32_t input_block_bits,
                                           const bool disable_literal_context_modeling,
                                           const EncoderMode mode, const uint32_t chunk_size)
    : chunk_size_{chunk_size}, state_(BrotliEncoderCreateInstance(nullptr, nullptr, nullptr),
                                      &BrotliEncoderDestroyInstance) {
  RELEASE_ASSERT(quality <= BROTLI_MAX_QUALITY, "");
  BROTLI_BOOL result = BrotliEncoderSetParameter(state_.get(), BROTLI_PARAM_QUALITY, quality);
  RELEASE_ASSERT(result == BROTLI_TRUE, "");

  RELEASE_ASSERT(window_bits >= BROTLI_MIN_WINDOW_BITS && window_bits <= BROTLI_MAX_WINDOW_BITS,
                 "");
  result = BrotliEncoderSetParameter(state_.get(), BROTLI_PARAM_LGWIN, window_bits);
  RELEASE_ASSERT(result == BROTLI_TRUE, "");

  RELEASE_ASSERT(input_block_bits >= BROTLI_MIN_INPUT_BLOCK_BITS &&
                     input_block_bits <= BROTLI_MAX_INPUT_BLOCK_BITS,
                 "");
  result = BrotliEncoderSetParameter(state_.get(), BROTLI_PARAM_LGBLOCK, input_block_bits);
  RELEASE_ASSERT(result == BROTLI_TRUE, "");

  result = BrotliEncoderSetParameter(state_.get(), BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING,
                                     disable_literal_context_modeling);
  RELEASE_ASSERT(result == BROTLI_TRUE, "");

  result = BrotliEncoderSetParameter(state_.get(), BROTLI_PARAM_MODE, static_cast<uint32_t>(mode));
  RELEASE_ASSERT(result == BROTLI_TRUE, "");
}

void BrotliCompressorImpl::compress(Buffer::Instance& buffer,
                                    Envoy::Compression::Compressor::State state) {
  Common::BrotliContext ctx(chunk_size_);

  Buffer::OwnedImpl accumulation_buffer;
  for (const Buffer::RawSlice& input_slice : buffer.getRawSlices()) {
    ctx.avail_in_ = input_slice.len_;
    ctx.next_in_ = static_cast<uint8_t*>(input_slice.mem_);

    while (ctx.avail_in_ > 0) {
      process(ctx, accumulation_buffer, BROTLI_OPERATION_PROCESS);
    }

    buffer.drain(input_slice.len_);
  }

  ASSERT(buffer.length() == 0);
  buffer.move(accumulation_buffer);

  // The encoder's internal buffer can still hold data not flushed to the
  // output chunk which in turn can be almost full and not fit to accommodate
  // the flushed data. And in case of the `Finish` operation the encoder may add
  // `ISLAST` and `ISLASTEMPTY` headers to the output. Thus keep processing
  // until the encoder's output is fully depleted.
  do {
    process(ctx, buffer,
            state == Envoy::Compression::Compressor::State::Finish ? BROTLI_OPERATION_FINISH
                                                                   : BROTLI_OPERATION_FLUSH);
  } while (BrotliEncoderHasMoreOutput(state_.get()));

  ctx.finalizeOutput(buffer);
}

void BrotliCompressorImpl::process(Common::BrotliContext& ctx, Buffer::Instance& output_buffer,
                                   const BrotliEncoderOperation op) {
  BROTLI_BOOL result = BrotliEncoderCompressStream(state_.get(), op, &ctx.avail_in_, &ctx.next_in_,
                                                   &ctx.avail_out_, &ctx.next_out_, nullptr);
  RELEASE_ASSERT(result == BROTLI_TRUE, "unable to compress");
  ctx.updateOutput(output_buffer);
}
} // namespace Compressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
