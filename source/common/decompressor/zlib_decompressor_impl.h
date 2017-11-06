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
  ZlibDecompressorImpl(uint64_t chunk_size);

  /**
   * Init must be called in order to initialize the decompressor and it should be always invoked
   * before calling decompress.
   * @param window_bits sets the size of the history buffer. It must be greater than or equal to
   * the window_bits value provided when data was compressed (zlib manual).
   */
  void init(int8_t window_bits);

  /**
   * Returns adler checksum
   */
  uint64_t checksum();

  /**
   * Implements Envoy::Decompressor
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
