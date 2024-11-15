#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/c_smart_ptr.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/grpc_tsi.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

/**
 * An interface to get callbacks from TsiHandshaker. TsiHandshaker will call this callbacks in the
 * thread which its dispatcher posts to.
 */
class TsiHandshakerCallbacks {
public:
  virtual ~TsiHandshakerCallbacks() = default;

  struct NextResult {
    // A status of the result.
    absl::Status status_;

    // The buffer to be sent to the peer.
    Buffer::InstancePtr to_send_;

    // A pointer to AltsHandshakeResult. Owned by instance.
    std::unique_ptr<AltsHandshakeResult> result_;
  };

  using NextResultPtr = std::unique_ptr<NextResult>;

  /**
   * Called when `next` is done, this may be called inline in `next` if the handshaker is not
   * asynchronous.
   * @param result a unique pointer to NextResult struct including the result returned by TSI.
   */
  virtual void onNextDone(NextResultPtr&& result) PURE;
};

/**
 * A C++ wrapper for tsi_handshaker interface.
 * For detail of tsi_handshaker, see
 * https://github.com/grpc/grpc/blob/v1.12.0/src/core/tsi/transport_security_interface.h#L236
 */
class TsiHandshaker final : public Event::DeferredDeletable {
public:
  explicit TsiHandshaker(std::unique_ptr<AltsTsiHandshaker> handshaker,
                         Event::Dispatcher& dispatcher);
  ~TsiHandshaker() override;

  /**
   * Conduct next step of handshake, see
   * https://github.com/grpc/grpc/blob/v1.12.0/src/core/tsi/transport_security_interface.h#L418
   * It is callers responsibility to not call this method again until the
   * TsiHandshakerCallbacks::onNextDone is called.
   * @param received the buffer received from peer.
   */
  absl::Status next(Buffer::Instance& received);

  /**
   * Set handshaker callbacks. This must be called before calling next.
   * @param callbacks supplies the callback instance.
   */
  void setHandshakerCallbacks(TsiHandshakerCallbacks& callbacks) { callbacks_ = &callbacks; }

  /**
   * Delete the handshaker when it is ready. This must be called after releasing from a smart
   * pointer. If there is no call in progress, this calls dispatcher_.deferredDelete(). If there is
   * a call in progress dispatcher_.deferredDelete happens after ongoing next call are processed.
   */
  void deferredDelete();

private:
  static void onNextDone(absl::Status status, void* handshaker, const unsigned char* bytes_to_send,
                         size_t bytes_to_send_size,
                         std::unique_ptr<AltsHandshakeResult> handshake_result);

  std::unique_ptr<AltsTsiHandshaker> handshaker_;
  TsiHandshakerCallbacks* callbacks_{};

  // This is set to true when there is an ongoing next call to handshaker, and set to false when
  // the callback posted to dispatcher_ by TsiHandshaker::onNextDone is executed.
  bool calling_{false};

  // This will be set when deferredDelete is called. If there is an ongoing next call,
  // the handshaker will delete itself after the call is processed.
  bool delete_on_done_{false};

  Event::Dispatcher& dispatcher_;
};

using TsiHandshakerPtr = std::unique_ptr<TsiHandshaker>;

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
