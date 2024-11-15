#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

/**
 * Interface for ListenerFilterBuffer
 */
class ListenerFilterBuffer {
public:
  virtual ~ListenerFilterBuffer() = default;

  /**
   * Return a single const raw slice to the buffer of the data.
   * @return a Buffer::ConstRawSlice pointed to raw buffer.
   */
  virtual const Buffer::ConstRawSlice rawSlice() const PURE;

  /**
   * Drain the data from the beginning of the buffer.
   * @param length the length of data to drain.
   * @return a bool indicate the drain is successful or not.
   */
  virtual bool drain(uint64_t length) PURE;
};

} // namespace Network
} // namespace Envoy
