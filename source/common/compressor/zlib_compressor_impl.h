#pragma once

#include "envoy/compressor/compressor.h"

#include "zlib.h"

namespace Envoy {
namespace Compressor {

/**
 * Implementation of compressor's interface.
 */
class ZlibCompressorImpl : public Compressor {
public:
  ZlibCompressorImpl();

  /**
   * Sets buffer size for feeding data to the compressor routines.
   * @param chunk amount of memory reserved for the compressor output.
   */
  ZlibCompressorImpl(uint64_t chunk);

  /**
   * Enum values used to set compression level during initialization.
   * best: gives best compression.
   * speed: gives best.
   * standard: gives normal compression. (default)
   */
  enum class CompressionLevel : int8_t {
    Best = 9,
    Speed = 1,
    Standard = -1,
  };

  /**
   * Enum values are used for setting the compression algorithm strategy.
   * filtered: used for data produced by a filter. (or predictor) @see Z_FILTERED (zlib manual)
   * huffman: used to enforce Huffman encoding. @see RFC 1951
   * rle: used to limit match distances to one. (Run-length encoding)
   * standard: used for normal data. (default) @see Z_DEFAULT_STRATEGY (zlib manual)
   */
  enum class CompressionStrategy : uint8_t {
    Filtered = 1,
    Huffman = 2,
    Rle = 3,
    Standard = 4,
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
  void init(CompressionLevel level, CompressionStrategy strategy, int8_t window_bits,
            uint8_t memory_level);

  /**
   * Flush should be called when no more data needs to be compressed. It will compress
   * any remaining input available in the compressor and flush the compressed data to the output
   * buffer. Note that forcing flush frequently degrades the compression ratio, so this should only
   * be called when necessary.
   * @param output_buffer supplies the buffer to output compressed data.
   */
  void flush(Buffer::Instance& output_buffer);

  /**
   * @return uint64_t CRC-32 if a gzip stream is being written or Adler-32 for other compression
   * types.
   */
  uint64_t checksum();

  /**
   * Implements Envoy::Compressor.
   */
  void compress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

private:
  bool deflateNext(int8_t flush_state);
  void process(Buffer::Instance& output_buffer, int8_t flush_state);
  void updateOutput(Buffer::Instance& output_buffer);

  uint64_t chunk_;
  bool initialized_;

  std::unique_ptr<unsigned char[]> output_char_ptr_;
  std::unique_ptr<z_stream, std::function<void(z_stream*)>> zstream_ptr_;
};

} // namespace Compressor
} // namespace Envoy
