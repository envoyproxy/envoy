#pragma once

#include "envoy/decompressor/decompressor.h"

#include "zlib.h"

namespace Envoy {
namespace Decompressor {

/**
 * Implementation of decompressor's interface.
 */
class ZlibDecompressorImpl : public Decompressor {
public:
  ZlibDecompressorImpl();

  /**
   * Constructor that allows setting the size of decompressor's output buffer. It
   * should be called whenever a buffer size different than the 4096 bytes, normally set by the
   * default constructor, is desired. If memory is available and it makes sense to output large
   * chunks of compressed data, zlib documentation suggests buffers sizes on the order of 128K or
   * 256K bytes. @see http://zlib.net/zlib_how.html
   * @param chunk_size amount of memory reserved for the decompressor output.
   */
  ZlibDecompressorImpl(uint64_t chunk_size);

  /**
   * Init must be called in order to initialize the decompressor. Once decompressor is initialized,
   * it cannot be initialized again. Init should run before decompressing any data.
   * @param window_bits sets the size of the history buffer. It must be greater than or equal to
   * the window_bits value provided when data was compressed (zlib manual).
   */
  void init(int64_t window_bits);

  /**
   * It returns the checksum of all output produced so far. Decompressor's checksum at the end of
   * the stream has to match compressor's checksum produced at the end of the compression.
   * @return uint64_t CRC-32 if a gzip stream is being read or Adler-32 for other compression
   * types.
   */
  uint64_t checksum();

  // Decompressor
  void decompress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

private:
  bool inflateNext();

  const uint64_t chunk_size_;
  bool initialized_;

  std::unique_ptr<unsigned char[]> chunk_char_ptr_;
  std::unique_ptr<z_stream, std::function<void(z_stream*)>> zstream_ptr_;
};

} // namespace Decompressor
} // namespace Envoy
