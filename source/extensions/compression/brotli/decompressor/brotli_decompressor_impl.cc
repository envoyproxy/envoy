#include "extensions/compression/brotli/decompressor/brotli_decompressor_impl.h"

#include <memory>

//#include "envoy/common/exception.h"

//#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Decompressor {

BrotliDecompressorImpl::BrotliDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix,
                                               uint64_t chunk_size,
                                               bool disable_ring_buffer_reallocation)
    : chunk_size_{chunk_size},
      state_(BrotliDecoderCreateInstance(NULL, NULL, NULL), &BrotliDecoderDestroyInstance),
      stats_(generateStats(stats_prefix, scope)) {
  BROTLI_BOOL result =
      BrotliDecoderSetParameter(state_.get(), BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION,
                                disable_ring_buffer_reallocation ? BROTLI_TRUE : BROTLI_FALSE);
  RELEASE_ASSERT(result == BROTLI_TRUE, "");
}

void BrotliDecompressorImpl::decompress(const Buffer::Instance& input_buffer,
                                        Buffer::Instance& output_buffer) {
  BrotliContext ctx(chunk_size_);
  bool success{true};

  for (const Buffer::RawSlice& input_slice : input_buffer.getRawSlices()) {
    ctx.avail_in = input_slice.len_;
    ctx.next_in = static_cast<uint8_t*>(input_slice.mem_);

    while (ctx.avail_in > 0 && success) {
      success = process(ctx, output_buffer);
    }
  }

  do {
    success = process(ctx, output_buffer);
  } while (BrotliDecoderHasMoreOutput(state_.get()) && !BrotliDecoderIsFinished(state_.get()) &&
           success);

  const size_t n_output = chunk_size_ - ctx.avail_out;
  if (n_output > 0) {
    output_buffer.add(static_cast<void*>(ctx.chunk_ptr.get()), n_output);
  }
}

bool BrotliDecompressorImpl::process(BrotliContext& ctx, Buffer::Instance& output_buffer) {
  BrotliDecoderResult result;
  result = BrotliDecoderDecompressStream(state_.get(), &ctx.avail_in, &ctx.next_in, &ctx.avail_out,
                                         &ctx.next_out, nullptr);
  if (result == BROTLI_DECODER_RESULT_ERROR) {
    // TODO(rojkov): currently the Brotli library doesn't specify possible errors in its API. Add
    // more detailled stats when they are documented.
    stats_.brotli_error_.inc();
    return false;
  }

  if (ctx.avail_out == 0) {
    // update output and reset context
    output_buffer.add(static_cast<void*>(ctx.chunk_ptr.get()), chunk_size_);
    ctx.chunk_ptr = std::make_unique<uint8_t[]>(chunk_size_);
    ctx.avail_out = chunk_size_;
    ctx.next_out = ctx.chunk_ptr.get();
  }

  return true;
}

} // namespace Decompressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
