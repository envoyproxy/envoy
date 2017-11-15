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
   * Compresses data from one buffer into another buffer.
   * @param input_buffer supplies the buffer with data to be compressed.
   * @param output_buffer supplies the buffer to output compressed data.
   */
  virtual void compress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) PURE;
};

} // namespace Compressor
} // namespace Envoy
