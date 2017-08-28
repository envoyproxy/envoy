#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Compressor {

/**
 * Allows compressing and decompressing data.
 */
class Compressor {
public:
  virtual ~Compressor() {}

  /**
   * Compresses data from one buffer into another buffer.
   * @param in supplies the buffer with data to be compressed.
   * @param out supplies the buffer to output compressed data.
   * @return bool indicating whether operation succeeded or not.
   */
  virtual bool compress(const Buffer::Instance& in, Buffer::Instance& out) PURE;

  /**
   * Decompresses encoded data from one buffer into another buffer
   * @param in supplies the buffer with data to be decompressed.
   * @param out supplies the buffer to output uncompressed data.
   * @return bool indicating whether operation succeeded or not.
   */
  virtual bool decompress(const Buffer::Instance& in, Buffer::Instance& out) PURE;
};

} // namespace Compressor
} // namespace Envoy
