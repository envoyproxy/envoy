#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "common/common/c_smart_ptr.h"

#include "extensions/transport_sockets/alts/grpc_tsi.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

/**
 * An interface to get callback from TsiHandshaker. TsiHandshaker will call this callback in the
 * thread which its dispatcher posts to.
 */
class TsiHandshakerCallbacks {
public:
  virtual ~TsiHandshakerCallbacks() {}

  struct NextResult {
    // A enum of the result
    tsi_result status_;

    // The buffer to be sent to the peer
    Buffer::InstancePtr to_send_;

    // A pointer to tsi_handshaker_result struct. Owned by instance.
    CHandshakerResultPtr result_;
  };

  typedef std::unique_ptr<NextResult> NextResultPtr;

  /**
   * Called when `next` is done, this may be called inline in `next` if the handshaker is not
   * asynchronous.
   * @param result
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
  explicit TsiHandshaker(CHandshakerPtr&& handshaker, Event::Dispatcher& dispatcher);
  ~TsiHandshaker();

  /**
   * Conduct next step of handshake, see
   * https://github.com/grpc/grpc/blob/v1.10.0/src/core/tsi/transport_security_interface.h#L416
   * It is callers responsibility to not call this method again until the onNextDone is called.
   * @param received the buffer received from peer.
   */
  tsi_result next(Buffer::Instance& received);

  /**
   * Set handshaker callbacks, this must be called before calling next.
   * @param callbacks supplies the callback instance.
   */
  void setHandshakerCallbacks(TsiHandshakerCallbacks& callbacks) { callbacks_ = &callbacks; }

  /**
   * Delete the handshaker when it is ready. This must be called after releasing from a smart
   * pointer. The actual delete happens after ongoing next call are processed.
   */
  void deferredDelete();

private:
  static void onNextDone(tsi_result status, void* user_data, const unsigned char* bytes_to_send,
                         size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result);

  CHandshakerPtr handshaker_;
  TsiHandshakerCallbacks* callbacks_{};

  // This is set to true when there is an ongoing next call to handshaker.
  bool calling_{false};

  // This will be set when deferredDelete is called. If there is an ongoing next call,
  // the handshaker will delete itself after the call is processed.
  bool delete_on_done_{false};

  Event::Dispatcher& dispatcher_;
};

typedef std::unique_ptr<TsiHandshaker> TsiHandshakerPtr;
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
