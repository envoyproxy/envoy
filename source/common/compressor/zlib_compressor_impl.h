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
    default_strategy = Z_DEFAULT_STRATEGY,
  };

  /**
   * Sets buffer size for feeding data to and pulling data from the zlib routines.
   * @param chunk amount of memory that is reserved for zlib to inflate/deflate data, default =
   * 4096.
   */
  void setChunk(uint64_t chunk);

  /**
   * Used for compression initialization.
   * @param level sets compression level. 'best' gives best compression, 'speed' gives best
   * performance, 'default_compression' gives normal compression. default = default_compression.
   * @param strategy sets the compression algorithm. 'default_strategy' is used for normal data,
   * 'filtered' is used for data produced by a predictor, 'huffman' is used to enforce Huffman
   * encoding, 'rle' is used to limit match distances to one. default = default_strategy.
   * @param window_bits sets the size of the history buffer. Larger values result in better
   * compression, but will use more memory. It normally ranges from 8 to 15, but larger values can
   * be applied, e.g. 31 (gzip). Larger window_bits will result in better compression at memory
   * cost. A negative window_bits strips off zlib header and checksum from the stream.
   * @param memory_level sets how much memory should be allocated for the internal compression
   * state. memory_level = 1 uses the least amount of memory but is slow and degrades compression
   * ratio, memory_level = 9 uses max memory for optimal speed.
   * @return bool true if initialization succceeded or has been already initialized and false if an
   * error occurred.
   */
  bool init(CompressionLevel level, CompressionStrategy strategy, int window_bits,
            uint memory_level);

  /**
   * Used for decompression initialization.
   * @param window_bits sets the size of the history buffer. Larger values result in better
   * decompression, but will use more memory. If the window_bits input is less than the one used
   * during compression, decompress will return false.
   * @return bool true if initialization succceeded or has been already initialized and false if an
   * error occurred.
   */
  bool init(int window_bits);

  /**
   * Returns the final state of the compression/decompression and resets zlib internal counters (e.g
   * total_in). This function does not free and reallocate the internal decompression state. The
   * stream will keep attributes that may have been set during initialization. It must be called
   * after calling init() and after some compression/decompression has been done.
   * @return bool true if success, or false if the source stream state was inconsistent.
   */
  bool reset();

  /**
   * Implements Envoy::Compressor
   */
  bool compress(const Buffer::Instance& in, Buffer::Instance& out) override;

  bool decompress(const Buffer::Instance& in, Buffer::Instance& out) override;

private:
  bool process(const Buffer::Instance& in, Buffer::Instance& out,
               int (*zlib_func_ptr)(z_stream*, int));

  std::unique_ptr<z_stream> zlib_ptr_{nullptr};

  int (*end_func_ptr_)(z_stream*){nullptr};
  int (*reset_func_ptr_)(z_stream*){nullptr};

  uint64_t chunk_{4096};
};

} // namespace Compressor
} // namespace Envoy
