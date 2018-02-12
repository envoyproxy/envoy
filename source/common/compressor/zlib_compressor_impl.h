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
   * standard: requests a default compromise between speed and compression. (default) @see zlib
   * manual.
   */
  enum class CompressionLevel : int64_t {
    Best = 9,
    Speed = 1,
    Standard = -1,
  };

  /**
   * Enum values are used for setting the compression algorithm strategy.
   * filtered: used for data produced by a filter. (or predictor) @see Z_FILTERED (zlib manual)
   * huffman: used to enforce Huffman encoding. @see RFC 1951
   * rle: used to limit match distances to one. (Run-length encoding)
   * standard: used for normal data. (default) @see Z_DEFAULT_STRATEGY in zlib manual.
   */
  enum class CompressionStrategy : uint64_t {
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
  void init(CompressionLevel level, CompressionStrategy strategy, int64_t window_bits,
            uint64_t memory_level);

  /**
   * Flush should be called when no more data needs to be compressed. It will compress
   * any remaining input available in the compressor and flush the compressed data to the output
   * buffer. Note that forcing flush frequently degrades the compression ratio, so this should only
   * be called when necessary.
   * @param output_buffer supplies the buffer to output compressed data.
   */
  void flush(Buffer::Instance& output_buffer);

  /**
   * It returns the checksum of all output produced so far. Compressor's checksum at the end of the
   * stream has to match decompressor's checksum produced at the end of the decompression.
   * @return uint64_t CRC-32 if a gzip stream is being written or Adler-32 for other compression
   * types.
   */
  uint64_t checksum();

  // Compressor
  void compress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

private:
  bool deflateNext(int64_t flush_state);
  void process(Buffer::Instance& output_buffer, int64_t flush_state);
  void updateOutput(Buffer::Instance& output_buffer);

  const uint64_t chunk_size_;
  bool initialized_;

  std::unique_ptr<unsigned char[]> chunk_char_ptr_;
  std::unique_ptr<z_stream, std::function<void(z_stream*)>> zstream_ptr_;
};

} // namespace Compressor
} // namespace Envoy
