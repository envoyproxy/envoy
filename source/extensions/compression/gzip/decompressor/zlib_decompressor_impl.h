#pragma once

#include "envoy/compression/decompressor/decompressor.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/compression/gzip/common/base.h"

#include "zlib.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Decompressor {

/**
 * All zlib decompressor stats. @see stats_macros.h
 */
#define ALL_ZLIB_DECOMPRESSOR_STATS(COUNTER)                                                       \
  COUNTER(zlib_errno)                                                                              \
  COUNTER(zlib_stream_error)                                                                       \
  COUNTER(zlib_data_error)                                                                         \
  COUNTER(zlib_mem_error)                                                                          \
  COUNTER(zlib_buf_error)                                                                          \
  COUNTER(zlib_version_error)

/**
 * Struct definition for zlib decompressor stats. @see stats_macros.h
 */
struct ZlibDecompressorStats {
  ALL_ZLIB_DECOMPRESSOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Implementation of decompressor's interface.
 */
class ZlibDecompressorImpl : public Zlib::Base,
                             public Envoy::Compression::Decompressor::Decompressor,
                             public Logger::Loggable<Logger::Id::decompression> {
public:
  ZlibDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix);

  /**
   * Constructor that allows setting the size of decompressor's output buffer. It
   * should be called whenever a buffer size different than the 4096 bytes, normally set by the
   * default constructor, is desired. If memory is available and it makes sense to output large
   * chunks of compressed data, zlib documentation suggests buffers sizes on the order of 128K or
   * 256K bytes. @see http://zlib.net/zlib_how.html
   * @param chunk_size amount of memory reserved for the decompressor output.
   */
  ZlibDecompressorImpl(Stats::Scope& scope, const std::string& stats_prefix, uint64_t chunk_size);

  /**
   * Init must be called in order to initialize the decompressor. Once decompressor is initialized,
   * it cannot be initialized again. Init should run before decompressing any data.
   * @param window_bits sets the size of the history buffer. It must be greater than or equal to
   * the window_bits value provided when data was compressed (zlib manual).
   */
  void init(int64_t window_bits);

  // Compression::Decompressor::Decompressor
  void decompress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

  // Flag to track whether error occurred during decompression.
  // When an error occurs, the error code (a negative int) will be stored in this variable.
  int decompression_error_{0};

private:
  // TODO: clean up friend class. This is here to allow coverage of chargeErrorStats as it isn't
  // completely straightforward
  // to cause zlib's inflate function to return all the error codes necessary to hit all the cases
  // in the switch statement.
  friend class ZlibDecompressorStatsTest;
  static ZlibDecompressorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return ZlibDecompressorStats{ALL_ZLIB_DECOMPRESSOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  bool inflateNext();
  void chargeErrorStats(const int result);

  const ZlibDecompressorStats stats_;
};

} // namespace Decompressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
