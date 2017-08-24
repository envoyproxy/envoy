#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Compressor {

/**
 * Public compressor allows compress and decompress buffer data.
 */
class Compressor {
public:
  virtual ~Compressor() {}

  /**
   * Compress data from one buffer into another buffer
   * @param in supplies the buffer with data to be compressed
   * @param out supplies the buffer to output compressed data
   * @return bool indicating whether operation succeeded or not.
   */
  virtual bool compress(Buffer::Instance& in, Buffer::Instance& out) PURE;

  /**
   * Decompress data from one buffer into another buffer
   * @param in supplies the buffer with data to be decompressed
   * @param out supplies the buffer to output uncompressed data
   * @return bool indicating whether operation succeeded or not.
   */
  virtual bool decompress(Buffer::Instance& in, Buffer::Instance& out) PURE;

  /**
   * Ends compression/decompression.
   * @return bool indicating whether operation succeeded or not.
   */
  virtual bool finish() PURE;
};

} // namespace Compressor
} // namespace Envoy
