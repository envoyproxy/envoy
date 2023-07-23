#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

// Shared state between peering user space IO handles.
class PassthroughState {
public:
  virtual ~PassthroughState() = default;

  /**
   * Initialize the passthrough state from the downstream. This should be
   * called at most once before `mergeInto`.
   */
  virtual void initialize(std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                          const StreamInfo::FilterState::Objects& filter_state_objects) PURE;

  /**
   * Merge the passthrough state into a recipient stream metadata and its
   * filter state. This should be called at most once after `initialize`.
   */
  virtual void mergeInto(envoy::config::core::v3::Metadata& metadata,
                         StreamInfo::FilterState& filter_state) PURE;
};

using PassthroughStateSharedPtr = std::shared_ptr<PassthroughState>;

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

  /**
   * @return shared state between peering user space IO handles.
   */
  virtual PassthroughStateSharedPtr passthroughState() PURE;
};
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
