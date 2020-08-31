#pragma once

#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "brotli/decode.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Decompressor {

/**
 * All brotli decompressor stats. @see stats_macros.h
 */
#define ALL_BROTLI_DECOMPRESSOR_STATS(COUNTER) COUNTER(brotli_error)

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
   * Constructor that allows setting the size of decompressor's output buffer. It
   * should be called whenever a buffer size different than the 4096 bytes, normally set by the
   * default constructor, is desired. If memory is available and it makes sense to output large
   * chunks of compressed data.
   * @param chunk_size amount of memory reserved for the decompressor output.
   * @param disable_ring_buffer_reallocation if true disables "canny" ring buffer allocation
   * strategy. Ring buffer is allocated according to window size, despite the real size of the
   * content.
   */
  BrotliDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix, uint64_t chunk_size,
                         bool disable_ring_buffer_reallocation);

  // Envoy::Compression::Decompressor::Decompressor
  void decompress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

private:
  struct BrotliContext {
    BrotliContext(size_t chunk_size)
        : chunk_ptr(new uint8_t[chunk_size]), next_in(nullptr), next_out(nullptr), avail_in(0),
          avail_out(chunk_size) {
      next_out = chunk_ptr.get();
    }

    std::unique_ptr<uint8_t[]> chunk_ptr;
    const uint8_t* next_in;
    uint8_t* next_out;
    size_t avail_in;
    size_t avail_out;
  };

  static BrotliDecompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return BrotliDecompressorStats{
        ALL_BROTLI_DECOMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  bool process(BrotliContext& ctx, Buffer::Instance& output_buffer);

  const uint64_t chunk_size_;
  std::unique_ptr<BrotliDecoderState, decltype(&BrotliDecoderDestroyInstance)> state_;
  const BrotliDecompressorStats stats_;
};

} // namespace Decompressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
