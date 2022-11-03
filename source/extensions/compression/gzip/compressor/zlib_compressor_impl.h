#pragma once

#include "envoy/compression/compressor/compressor.h"

#include "source/extensions/compression/gzip/common/base.h"

#include "zlib.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Compressor {

/**
 * Implementation of compressor's interface.
 */
class ZlibCompressorImpl : public Common::Base, public Envoy::Compression::Compressor::Compressor {
public:
  ZlibCompressorImpl();

  /**
   * Constructor that allows setting the size of compressor's output buffer. It
   * should be called whenever a buffer size different than the 4096 bytes, normally set by the
   * default constructor, is desired. If memory is available and it makes sense to output large
   * chunks of compressed data, zlib documentation suggests buffers sizes on the order of 128K or
   * 256K bytes. @see http://zlib.net/zlib_how.html
   * @param chunk_size amount of memory reserved for the compressor output.
   */
  ZlibCompressorImpl(uint64_t chunk_size);

  /**
   * Enum values used to set compression level during initialization.
   * best: gives best compression.
   * speed: gives best performance.
   * levelX: allows to adjust trad-offs more precisely - from level1 (best speed, but very
   * low compression ratio) to level9 (best compression, but low speed).
   * standard: requests a default compromise between speed and compression. (default) @see zlib
   * manual.
   */
  enum class CompressionLevel : int64_t {
    Best = Z_BEST_COMPRESSION,
    Level1 = 1,
    Level2 = 2,
    Level3 = 3,
    Level4 = 4,
    Level5 = 5,
    Level6 = 6,
    Level7 = 7,
    Level8 = 8,
    Level9 = 9,
    Speed = Z_BEST_SPEED,
    Standard = Z_DEFAULT_COMPRESSION,
  };

  /**
   * Enum values are used for setting the compression algorithm strategy.
   * filtered: used for data produced by a filter. (or predictor) @see Z_FILTERED (zlib manual)
   * fixed: disable dynamic Huffman codes. @see Z_FIXED (zlib manual)
   * huffman: used to enforce Huffman encoding. @see RFC 1951
   * rle: used to limit match distances to one. (Run-length encoding)
   * standard: used for normal data. (default) @see Z_DEFAULT_STRATEGY in zlib manual.
   */
  enum class CompressionStrategy : uint64_t {
    Filtered = Z_FILTERED,
    Fixed = Z_FIXED,
    Huffman = Z_HUFFMAN_ONLY,
    Rle = Z_RLE,
    Standard = Z_DEFAULT_STRATEGY,
  };

  /**
   * Init must be called in order to initialize the compressor. Once compressor is initialized, it
   * cannot be initialized again. Init should run before compressing any data.
   * @param level @see CompressionLevel enum
   * @param strategy @see CompressionStrategy enum
   * @param window_bits sets the size of the history buffer. Larger values result in better
   * compression, but will use more memory @see window_bits. (zlib manual)
   * @param memory_level sets how much memory should be allocated for the internal compression, min
   * 1 and max 9. @see memory_level (zlib manual)
   */
  void init(CompressionLevel level, CompressionStrategy strategy, int64_t window_bits,
            uint64_t memory_level);

  // Compression::Compressor::Compressor
  void compress(Buffer::Instance& buffer, Envoy::Compression::Compressor::State state) override;

private:
  bool deflateNext(int64_t flush_state);
  void process(Buffer::Instance& output_buffer, int64_t flush_state);
};

} // namespace Compressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
