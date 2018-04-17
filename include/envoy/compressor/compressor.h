#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Compressor {

/**
 * Compressor state whether to flush the compressor or to finish the compression stream.
 */
enum class State { Flush, Finish };

/**
 * Allows compressing data.
 */
class Compressor {
public:
  virtual ~Compressor() {}

  /**
   * Compresses data buffer.
   * @param buffer supplies the reference to data to be compressed.
   * @param state supplies the compressor state.
   */
  virtual void compress(Buffer::Instance& buffer, State state) PURE;
};

} // namespace Compressor
} // namespace Envoy
