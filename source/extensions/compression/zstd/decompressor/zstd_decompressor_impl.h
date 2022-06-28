#pragma once

#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/compression/zstd/common/base.h"
#include "source/extensions/compression/zstd/common/dictionary_manager.h"

#include "zstd_errors.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Decompressor {

using ZstdDDictManager =
    Common::DictionaryManager<ZSTD_DDict, ZSTD_freeDDict, ZSTD_getDictID_fromDDict>;
using ZstdDDictManagerPtr = std::unique_ptr<ZstdDDictManager>;

/**
 * All zstd decompressor stats. @see stats_macros.h
 */
#define ALL_ZSTD_DECOMPRESSOR_STATS(COUNTER)                                                       \
  COUNTER(zstd_generic_error)                                                                      \
  COUNTER(zstd_dictionary_error)                                                                   \
  COUNTER(zstd_checksum_wrong_error)                                                               \
  COUNTER(zstd_memory_error)

/**
 * Struct definition for zstd decompressor stats. @see stats_macros.h
 */
struct ZstdDecompressorStats {
  ALL_ZSTD_DECOMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Implementation of decompressor's interface.
 */
class ZstdDecompressorImpl : public Common::Base,
                             public Envoy::Compression::Decompressor::Decompressor,
                             public Logger::Loggable<Logger::Id::decompression>,
                             NonCopyable {
public:
  ZstdDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix,
                       const ZstdDDictManagerPtr& ddict_manager, uint32_t chunk_size);

  // Envoy::Compression::Decompressor::Decompressor
  void decompress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

private:
  static ZstdDecompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return ZstdDecompressorStats{ALL_ZSTD_DECOMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  friend class ZstdDecompressorStatsTest;
  bool process(Buffer::Instance& output_buffer);
  bool isError(size_t result);

  std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> dctx_;
  const ZstdDDictManagerPtr& ddict_manager_;
  const ZstdDecompressorStats stats_;
  bool is_dictionary_set_{false};
};

} // namespace Decompressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
