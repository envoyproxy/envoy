#pragma once

#include <cstdint>
#include <memory>

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
   * Copy the bufferred data into the address which `buffer` pointed to.
   * But it won't drain the data after copyOut, except an explicit drain method invoked.
   * @param buffer supplies the buffer to read into.
   * @param max_length supplies the maximum length to read.
   * @return the length of data read into the buffer.
   */
  virtual uint64_t copyOut(void* buffer, uint64_t length) PURE;

  /**
   * Drain the data from the beginning of the buffer.
   * @param length the length of data to drain.
   * @return the actual length of data drained.
   */
  virtual uint64_t drain(uint64_t length) PURE;

  /**
   * Return the length of data in the buffer
   * @return length The length of data in the buffer.
   */
  virtual uint64_t length() const PURE;
};

} // namespace Network
} // namespace Envoy