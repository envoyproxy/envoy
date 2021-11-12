#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Compression {
namespace Decompressor {

/**
 * Allows decompressing data.
 */
class Decompressor {
public:
  virtual ~Decompressor() = default;

  /**
   * Decompresses data from one buffer into another buffer.
   * @param input_buffer supplies the buffer with compressed data.
   * @param output_buffer supplies the buffer to output decompressed data.
   */
  virtual void decompress(const Buffer::Instance& input_buffer,
                          Buffer::Instance& output_buffer) PURE;
};

using DecompressorPtr = std::unique_ptr<Decompressor>;

} // namespace Decompressor
} // namespace Compression
} // namespace Envoy
