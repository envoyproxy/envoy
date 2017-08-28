
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

  virtual bool compress(const Buffer::Instance& in, Buffer::Instance& out) PURE;

  virtual bool decompress(const Buffer::Instance& in, Buffer::Instance& out) PURE;
};

} // namespace Compressor
} // namespace Envoy
