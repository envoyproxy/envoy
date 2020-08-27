#pragma once

#include "envoy/event/dispatcher.h"

#include "common/common/logger.h"
#include "common/network/filter_manager_impl.h"

namespace Envoy {
namespace Network {

class ConnectionImplBase : public FilterManagerConnection,
                           protected Logger::Loggable<Logger::Id::connection> {
public:
  /**
   * Add a connection id to a hash key
   * @param hash_key the current hash key -- the function will only append to this vector
   * @param connection_id the 64-bit connection id
   */
  static void addIdToHashKey(std::vector<uint8_t>& hash_key, uint64_t connection_id);

  ConnectionImplBase(Event::Dispatcher& dispatcher, uint64_t id);

  // Network::Connection
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  uint64_t id() const override { return id_; }
  void hashKey(std::vector<uint8_t>& hash) const override;
  void setConnectionStats(const ConnectionStats& stats) override;
  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override;

protected:
  void initializeDelayedCloseTimer();

  bool inDelayedClose() const { return delayed_close_state_ != DelayedCloseState::None; }

  void raiseConnectionEvent(ConnectionEvent event);

  virtual void closeConnectionImmediately() PURE;

  // States associated with delayed closing of the connection (i.e., when the underlying socket is
  // not immediately close()d as a result of a ConnectionImpl::close()).
  enum class DelayedCloseState {
    None,
    // The socket will be closed immediately after the buffer is flushed _or_ if a period of
    // inactivity after the last write event greater than or equal to delayed_close_timeout_ has
    // elapsed.
    CloseAfterFlush,
    // The socket will be closed after a grace period of delayed_close_timeout_ has elapsed after
    // the socket is flushed _or_ if a period of inactivity after the last write event greater than
    // or equal to delayed_close_timeout_ has elapsed.
    CloseAfterFlushAndWait
  };
  DelayedCloseState delayed_close_state_{DelayedCloseState::None};

  Event::TimerPtr delayed_close_timer_;
  std::chrono::milliseconds delayed_close_timeout_{0};
  Event::Dispatcher& dispatcher_;
  const uint64_t id_;
  std::list<ConnectionCallbacks*> callbacks_;
  std::unique_ptr<ConnectionStats> connection_stats_;

private:
  // Callback issued when a delayed close timeout triggers.
  void onDelayedCloseTimeout();
};

} // namespace Network
} // namespace Envoy
