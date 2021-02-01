#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

/**
 * The interface for the peer as a writer and supplied read status query.
 */
class IoHandle {
public:
  virtual ~IoHandle() = default;

  /**
   * Set the flag to indicate no further write from peer.
   */
  virtual void setWriteEnd() PURE;

  /**
   * @return true if the peer promise no more write.
   */
  virtual bool isPeerShutDownWrite() const PURE;

  /**
   * Raised when peer is destroyed. No further write to peer is allowed.
   */
  virtual void onPeerDestroy() PURE;

  /**
   * Notify that consumable data arrived. The consumable data can be either data to read, or the end
   * of stream event.
   */
  virtual void setNewDataAvailable() PURE;

  /**
   * @return the buffer to be written.
   */
  virtual Buffer::Instance* getWriteBuffer() PURE;

  /**
   * @return true if more data is acceptable at the destination buffer.
   */
  virtual bool isWritable() const PURE;

  /**
   * @return true if peer is valid and writable.
   */
  virtual bool isPeerWritable() const PURE;

  /**
   * Raised by the peer when the peer switch from high water mark to low.
   */
  virtual void onPeerBufferLowWatermark() PURE;

  /**
   * @return true if the pending receive buffer is not empty or read_end is set.
   */
  virtual bool isReadable() const PURE;
};
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
