#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace BufferedIoSocket {

/**
 * The interface for the writer.
 */
class WritablePeer {
public:
  virtual ~WritablePeer() = default;

  /**
   * Set the flag to indicate no further write from peer.
   */
  virtual void setWriteEnd() PURE;
  /**
   * @return true if the peer promise no more write.
   */
  virtual bool isWriteEndSet() PURE;

  /**
   * Raised when peer is destroyed. No further write to peer is allowed.
   */
  virtual void onPeerDestroy() PURE;

  /**
   * Notify that consumable data arrives. The consumable data can be either data to read, or the end
   * of stream event.
   */
  virtual void maybeSetNewData() PURE;

  /**
   * @return the buffer to be written.
   */
  virtual Buffer::Instance* getWriteBuffer() PURE;

  /**
   * @return false more data is acceptable.
   */
  virtual bool isWritable() const PURE;

  /**
   * @return true if peer is valid and writable.
   */
  virtual bool isPeerWritable() const PURE;

  /**
   * Raised by the peer when the peer switch from high water mark to low.
   */
  virtual void onPeerBufferWritable() PURE;
};

/**
 * The interface as the union of ReadableSource and WritablePeer.
 */
class ReadWritable : public WritablePeer {
public:
  ~ReadWritable() override = default;

  /**
   * Read the flag to indicate no further write. Used by early close detection.
   */
  virtual bool isPeerShutDownWrite() const PURE;

  /**
   * @return true if the pending receive buffer is not full.
   */
  virtual bool isReadable() const PURE;
};
} // namespace BufferedIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
