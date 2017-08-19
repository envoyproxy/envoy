
// include/envoy/zlib.h

#pragma once

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Zlib {

/**
 * Public Zlib allows deflating buffer data.
 */
class Zlib {
public:
  virtual ~Zlib() {}

  /**
   * Deflates buffer data
   * @param data supplies the buffer with data to be deflated.
   * @return bool indicates whether operation succeeds or not.
   */
  virtual bool deflateData(Buffer::Instance& in) PURE;

};

typedef std::unique_ptr<Zlib> ZlibPtr;

} // namespace Zlib
} // namespace Envoy
