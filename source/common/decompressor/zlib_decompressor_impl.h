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
   * Sets buffer size for feeding data to the decompressor routines.
   * @param chunk amount of memory reserved for the compressor output.
   */
  ZlibDecompressorImpl(uint64_t chunk_size);

  /**
   * Init must be called in order to initialize the decompressor. Once decompressor is initialized,
   * it cannot be initialized again. Init should run before decompressing any data.
   * @param window_bits sets the size of the history buffer. It must be greater than or equal to
   * the window_bits value provided when data was compressed (zlib manual).
   */
  void init(int8_t window_bits);

  /**
   * @return uint64_t CRC-32 if a gzip stream is being written or Adler-32 for other compression
   * types.
   */
  uint64_t checksum();

  /**
   * Implements Envoy::Decompressor.
   */
  void decompress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

private:
  bool inflateNext();

  uint64_t chunk_;
  bool initialized_;

  std::unique_ptr<unsigned char[]> output_char_ptr_;
  std::unique_ptr<z_stream, std::function<void(z_stream*)>> zstream_ptr_;
};

} // namespace Decompressor
} // namespace Envoy
