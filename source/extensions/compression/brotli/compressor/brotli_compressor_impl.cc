#include "extensions/compression/brotli/compressor/brotli_compressor_impl.h"

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
  BrotliContext ctx(chunk_size_);

  for (const Buffer::RawSlice& input_slice : buffer.getRawSlices()) {
    ctx.avail_in = input_slice.len_;
    ctx.next_in = static_cast<uint8_t*>(input_slice.mem_);

    while (ctx.avail_in > 0) {
      process(ctx, buffer, BROTLI_OPERATION_PROCESS);
    }

    buffer.drain(input_slice.len_);
  }

  do {
    process(ctx, buffer,
            state == Envoy::Compression::Compressor::State::Finish ? BROTLI_OPERATION_FINISH
                                                                   : BROTLI_OPERATION_FLUSH);
  } while (BrotliEncoderHasMoreOutput(state_.get()) && !BrotliEncoderIsFinished(state_.get()));

  const size_t n_output = chunk_size_ - ctx.avail_out;
  if (n_output > 0) {
    buffer.add(static_cast<void*>(ctx.chunk_ptr.get()), n_output);
  }
}

void BrotliCompressorImpl::process(BrotliContext& ctx, Buffer::Instance& output_buffer,
                                   const BrotliEncoderOperation op) {
  BROTLI_BOOL result = BrotliEncoderCompressStream(state_.get(), op, &ctx.avail_in, &ctx.next_in,
                                                   &ctx.avail_out, &ctx.next_out, nullptr);
  RELEASE_ASSERT(result == BROTLI_TRUE, "");
  if (ctx.avail_out == 0) {
    // update output and reset context
    output_buffer.add(static_cast<void*>(ctx.chunk_ptr.get()), chunk_size_);
    ctx.chunk_ptr = std::make_unique<uint8_t[]>(chunk_size_);
    ctx.avail_out = chunk_size_;
    ctx.next_out = ctx.chunk_ptr.get();
  }
}
} // namespace Compressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
