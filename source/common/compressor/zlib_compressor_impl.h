#pragma once

#include "envoy/compressor/compressor.h"

#include "common/common/assert.h"

#include "zlib.h"

namespace Envoy {
namespace Compressor {

/**
 * Deflate implementation of compressor's interface.
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
   * Gets current total bytes passed into the compressor
   * @return current bytes moved into the compressor.
   */
  uint64_t getTotalIn();

  /**
   * Gets current total bytes moved out of the compressor
   * @return bytes returned by the compressor.
   */
  uint64_t getTotalOut();

  /**
   * Sets how much memory should be allocated by zlib
   * for the internal compression state.
   * @param mem_level level of allocated memory, default = 8 | max = 9.
   */
  void setMemoryLevel(uint mem_level);

  /**
   * Buffer size for feeding data to and pulling data from the zlib routines.
   * For this implementation, it should never be less than the length of evbuffer
   * passsed into ZlibCompressorImpl::start(Buffer::Instance& in, Buffer::Instance& out).
   * @param chunk available memory to deflate data, default = 4096.
   */
  void setChunk(uint64_t chunk);

  /**
   * The windowBits parameter shall be a base 2 logarithm of the window size
   * to use, and shall be a value between 8 and 15 (15 + 16 to write a simple gzip).
   * A smaller value will use less memory, but will result in a poorer compression
   * ratio, while a higher value will give better compression but utilize more memory.
   * @param window_bits memory window size, default = 31 gzip.
   */
  void setWindowBits(int window_bits);

  /**
   * Initialize the compression engine and allows to adjust compression level
   * as well strategy
   * @param level compression level applied, default = Z_DEFAULT_COMPRESSION.
   * @param strategy compression strategy applied, default = Z_DEFAULT_STRATEGY.
   * @return true if initialization succceeded or false if an error occurred
   */
  bool init(CompressionLevel level = CompressionLevel::default_compression,
            CompressionStrategy strategy = CompressionStrategy::default_strategy);

  /**
   * Finalize the compression engine and free all dynamic memory allocated during
   * compression. If this function does not get called, distructor will take care
   * of the memmory by calling deflateEnd(). Calling this method though allows to
   * inspect the compressor has errors.
   * @return true if compressor finalized without error or false otherwise.
   */
  bool finish() override;

  /**
   * Move and encode data from one buffer into another
   * @return true if compression succeeded or false otherwise.
   */
  bool compress(Buffer::Instance& in, Buffer::Instance& out) override;

  /**
   * Move and decode data from one buffer into another
   * @return true if decompression succeeded or false otherwise.
   */
  bool decompress(Buffer::Instance& in, Buffer::Instance& out) override;

private:
  std::unique_ptr<z_stream> ZlibPtr_{nullptr};
  uint64_t chunk_{4096};
  uint memory_level_{8};
  int window_bits_{15 + 16};
};

} // namespace Compressor
} // namespace Envoy