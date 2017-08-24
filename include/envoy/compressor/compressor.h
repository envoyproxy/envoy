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
   * Compress/Decompress data from one buffer into another buffer
   * @param in supplies the buffer with data to be compressed 
   * @param data supplies the buffer to output compressed data
   * @return bool indicating whether operation succeeded or not.
   */
  virtual bool start(Buffer::Instance& in, Buffer::Instance& out) PURE;
  
  /**
   * Ends compression/decompression.
   * @return bool indicating whether operation succeeded or not.
   */
  virtual bool finish() PURE;
  
};

} // namespace Compressor
} // namespace Envoy
