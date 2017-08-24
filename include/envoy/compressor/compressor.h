
// include/envoy/zlib.h

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
   * @param data supplies the buffer to output compressed data
   * @return bool indicates whether operation succeeded or not.
   */
  virtual bool start(Buffer::Instance& in, Buffer::Instance& out) PURE;
  
  /**
   * Clean up any internal memory allocated by the compressor.
   */
  virtual bool finish() PURE;
  
};

} // namespace Compressor
} // namespace Envoy
