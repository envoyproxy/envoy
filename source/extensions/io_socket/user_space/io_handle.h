#pragma once

#include <functional>

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

  /**
   * Capture filter state objects marked SharedWithDownstreamConnectionOnClose
   * from the upstream-side (inner) connection's filter state at upstream close.
   * Stores captured objects internally for later delivery via `mergeReverse`.
   * Called at most once.
   */
  virtual void captureReverse(const StreamInfo::FilterState& filter_state) PURE;

  /**
   * Merge previously-captured reverse-propagation objects into the
   * downstream-side (outer) connection's filter state. Called at most once
   * after `captureReverse`. Safe to call without a prior capture (no-op).
   */
  virtual void mergeReverse(StreamInfo::FilterState& filter_state) PURE;
};

using PassthroughStateSharedPtr = std::shared_ptr<PassthroughState>;
using PassthroughStatePtr = std::unique_ptr<PassthroughState>;

/**
 * The interface for the peer as a writer and supplied read status query.
 */
class IoHandle {
public:
  virtual ~IoHandle() = default;

  /**
   * Called by the peer to indicate that it will not send any more data.
   */
  virtual void setEof() PURE;

  /**
   * @return true if the peer has indicated that it will not send any more data.
   */
  virtual bool hasReceivedEof() const PURE;

  /**
   * Raised when peer is destroyed. Sending any more data to the peer will fail.
   */
  virtual void onPeerDestroy() PURE;

  /**
   * Notify that consumable data arrived. The consumable data can be data in the receive buffer, or
   * the end of stream event.
   */
  virtual void setNewDataAvailable() PURE;

  /**
   * @return the buffer holding data received from the peer.
   */
  virtual Buffer::Instance* getReceiveBuffer() PURE;

  /**
   * @return true if the receive buffer can accept more data from the peer.
   */
  virtual bool canReceiveData() const PURE;

  /**
   * @return true if the peer is valid and its receive buffer can accept more data, or if the peer
   * is no longer open for reads. Either means that write() calls to this handle will not block.
   */
  virtual bool isWriteUnblocked() const PURE;

  /**
   * Raised by the peer when its receive buffer switches from high watermark to low watermark.
   */
  virtual void onPeerBufferLowWatermark() PURE;

  /**
   * @return true if the receive buffer is not empty or read_end is set. This means that read()
   * calls to this handle will not block.
   */
  virtual bool isReadable() const PURE;

  /**
   * @return shared state between peering user space IO handles.
   */
  virtual PassthroughStateSharedPtr passthroughState() PURE;

  /**
   * Register a callback to be invoked exactly once, when this handle is about
   * to close. Used to capture filter state from the owning connection's
   * stream info before the connection is torn down (reverse passthrough
   * propagation across the internal listener boundary).
   */
  virtual void addOnPreCloseCallback(std::function<void()> callback) PURE;
};
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
