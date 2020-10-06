#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/proxy_protocol.h"
#include "envoy/ssl/connection.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

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
   * Raised by the peer when the peer switch from high water mark to low.
   */
  virtual void onPeerBufferWritable() PURE;

  //   virtual bool triggeredHighToLowWatermark() const PURE;
  //   virtual void clearTriggeredHighToLowWatermark() PURE;
  //   virtual void setTriggeredHighToLowWatermark() PURE;
};

/**
 * The interface for the buffer owner who want to consume the buffer.
 */
class ReadableSource {
public:
  virtual ~ReadableSource() = default;

  /**
   * Read the flag to indicate no further write. Used by early close detection.
   */
  virtual bool isPeerShutDownWrite() const PURE;

  virtual bool isOverHighWatermark() const PURE;
  virtual bool isReadable() const PURE;
};
} // namespace Network
} // namespace Envoy