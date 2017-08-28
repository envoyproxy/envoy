#pragma once

#include "envoy/compressor/compressor.h"

#include "zlib.h"

namespace Envoy {
namespace Compressor {

/**
 * Zlib implementation of compressor's interface.
 */
class ZlibCompressorImpl : public Compressor {
public:
  ZlibCompressorImpl();
  ~ZlibCompressorImpl();

  enum CompressionLevel {
    default_compression = Z_DEFAULT_COMPRESSION,
    best = Z_BEST_COMPRESSION,
    speed = Z_BEST_SPEED,
  };

  enum CompressionStrategy {
    filtered = Z_FILTERED,
    huffman = Z_HUFFMAN_ONLY,
    rle = Z_RLE,
    fixed = Z_FIXED,
    default_strategy = Z_DEFAULT_STRATEGY,
  };

  /**
   * Sets buffer size for feeding data to and pulling data from the zlib routines.
   * @param chunk amout of memory that is reserved for zlib to inflate/deflate data, default = 4096.
   */
  void setChunk(uint64_t chunk);

  /**
   * Used for compression initialization.
   * @param level compression level applied, default = default_compression.
   * @param strategy compression strategy applied, default = default_strategy.
   * @param window_bits allows setting the zlib's window bits for different encoding types,
   * default = 31 (gzip encoding).
   * @param memory_level allows setting how much memory should be allocated for the internal
   * compression state, default = 8.
   * @return bool true if initialization succceeded or has been already initialized and false if an
   * error occurred.
   */
  bool init(CompressionLevel level, CompressionStrategy strategy, int window_bits,
            uint memory_level);

  /**
   * Used for decompression initialization.
   * @param window_bits is used for setting different encoding types. It should be set as the same
   * as used during compression, but if that's not known, setting window bits to zero will tell the
   * decompressor to use whatever is set in the header of the encoded data. default = 31 (gzip
   * encoding).
   * @return bool true if initialization succceeded or has been already initialized and false if an
   * error occurred.
   */
  bool init(int window_bits);

  /**
   * Returns the final state of the compression and resets zlib internal counters (e.g total_in).
   * This function does not free and reallocate the internal decompression state. The The stream
   * will keep attributes that may have been set during initialization.
   * @return bool true if success, or false if the source stream state was inconsistent.
   */
  bool finish();

  /**
   * Implements Envoy::Compressor
   */
  bool compress(const Buffer::Instance& in, Buffer::Instance& out) override;

  bool decompress(const Buffer::Instance& in, Buffer::Instance& out) override;

private:
  bool process(const Buffer::Instance& in, Buffer::Instance& out,
               int (*zlib_func_ptr)(z_stream*, int));

  std::unique_ptr<z_stream> zlib_ptr_{nullptr};

  uint64_t chunk_{4096};
  bool is_deflate_{true};
  bool is_initialized_{false};
};

} // namespace Compressor
} // namespace Envoy
