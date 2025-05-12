#pragma once

#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/compression/brotli/common/base.h"

#include "brotli/decode.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Decompressor {

/**
 * All brotli decompressor stats. @see stats_macros.h
 */
#define ALL_BROTLI_DECOMPRESSOR_STATS(COUNTER)                                                     \
  COUNTER(brotli_error)           /*Decompression error of all.*/                                  \
  COUNTER(brotli_output_overflow) /*Decompression error because of the overflow output.*/          \
  COUNTER(brotli_redundant_input) /*Decompression error because of the redundant input.*/

/**
 * Struct definition for brotli decompressor stats. @see stats_macros.h
 */
struct BrotliDecompressorStats {
  ALL_BROTLI_DECOMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Implementation of decompressor's interface.
 */
class BrotliDecompressorImpl : public Envoy::Compression::Decompressor::Decompressor, NonCopyable {
public:
  /**
   * Constructor.
   * @param chunk_size amount of memory reserved for the decompressor output.
   * @param disable_ring_buffer_reallocation if true disables "canny" ring buffer allocation
   * strategy. Ring buffer is allocated according to window size, despite the real size of the
   * content.
   */
  BrotliDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix,
                         const uint32_t chunk_size, bool disable_ring_buffer_reallocation);

  // Envoy::Compression::Decompressor::Decompressor
  void decompress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

private:
  static BrotliDecompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return BrotliDecompressorStats{
        ALL_BROTLI_DECOMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  bool process(Common::BrotliContext& ctx, Buffer::Instance& output_buffer);

  const uint32_t chunk_size_;
  std::unique_ptr<BrotliDecoderState, decltype(&BrotliDecoderDestroyInstance)> state_;
  const BrotliDecompressorStats stats_;
};

} // namespace Decompressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
