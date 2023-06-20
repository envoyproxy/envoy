#include "source/extensions/compression/brotli/decompressor/brotli_decompressor_impl.h"

#include <memory>

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Decompressor {

namespace {

// How many times the output buffer is allowed to be bigger than the input
// buffer. This value is used to detect compression bombs.
// TODO(rojkov): Re-design the Decompressor interface to handle compression
// bombs gracefully instead of this quick solution.
constexpr uint32_t MaxInflateRatio = 100;

} // namespace

BrotliDecompressorImpl::BrotliDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix,
                                               const uint32_t chunk_size,
                                               const bool disable_ring_buffer_reallocation)
    : chunk_size_{chunk_size},
      state_(BrotliDecoderCreateInstance(nullptr, nullptr, nullptr), &BrotliDecoderDestroyInstance),
      stats_(generateStats(stats_prefix, scope)) {
  BROTLI_BOOL result =
      BrotliDecoderSetParameter(state_.get(), BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION,
                                disable_ring_buffer_reallocation ? BROTLI_TRUE : BROTLI_FALSE);
  RELEASE_ASSERT(result == BROTLI_TRUE, "");
}

void BrotliDecompressorImpl::decompress(const Buffer::Instance& input_buffer,
                                        Buffer::Instance& output_buffer) {
  Common::BrotliContext ctx(chunk_size_, MaxInflateRatio * input_buffer.length());

  for (const Buffer::RawSlice& input_slice : input_buffer.getRawSlices()) {
    ctx.avail_in_ = input_slice.len_;
    ctx.next_in_ = static_cast<uint8_t*>(input_slice.mem_);

    while (ctx.avail_in_ > 0) {
      if (!process(ctx, output_buffer)) {
        ctx.finalizeOutput(output_buffer);
        return;
      }
    }
  }

  // Even though the input has been fully consumed by the decoder it still can
  // be unfolded into output not fitting the output chunk. Thus keep processing
  // until the decoder's output is fully depleted.
  bool success;
  do {
    success = process(ctx, output_buffer);
  } while (success && BrotliDecoderHasMoreOutput(state_.get()));

  ctx.finalizeOutput(output_buffer);
}

bool BrotliDecompressorImpl::process(Common::BrotliContext& ctx, Buffer::Instance& output_buffer) {
  BrotliDecoderResult result;
  result = BrotliDecoderDecompressStream(state_.get(), &ctx.avail_in_, &ctx.next_in_,
                                         &ctx.avail_out_, &ctx.next_out_, nullptr);
  if (result == BROTLI_DECODER_RESULT_ERROR) {
    // TODO(rojkov): currently the Brotli library doesn't specify possible errors in its API. Add
    // more detailed stats when they are documented.
    stats_.brotli_error_.inc();
    return false;
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.enable_compression_bomb_protection") &&
      (output_buffer.length() > ctx.max_output_size_)) {
    stats_.brotli_error_.inc();
    return false;
  }

  ctx.updateOutput(output_buffer);

  return true;
}

} // namespace Decompressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
