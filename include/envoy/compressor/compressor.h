#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Compressor {

/**
 * Allows compressing data.
 */
class Compressor {
public:
  virtual ~Compressor() {}

  /**
   * Compresses data buffer.
   * @param buffer supplies the reference to data to be compressed.
   * @param trailer supplies the indication to write trailer at the end of compressed buffer.
   */
  virtual void compress(Buffer::Instance& buffer, bool trailer) PURE;
};

} // namespace Compressor
} // namespace Envoy
