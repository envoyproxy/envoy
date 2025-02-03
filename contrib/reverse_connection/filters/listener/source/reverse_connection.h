#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "contrib/reverse_connection/filters/listener/source/config.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_conn_global_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

namespace ReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

enum class ReadOrParseState { Done, TryAgainLater, Error };

/**
 * Listener filter to store reverse connections until they are actively used.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const Config& config, std::shared_ptr<ReverseConnection::ReverseConnRegistry> reverse_conn_registry);
  ~Filter();

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  size_t maxReadBytes() const override;
  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override;
  void onClose() override;
  ReverseConnection::ReverseConnectionManager& reverseConnectionManager() {
    ReverseConnection::RCThreadLocalRegistry* thread_local_registry = reverse_conn_registry_->getLocalRegistry();
    if (thread_local_registry == nullptr) {
      throw EnvoyException("Cannot get ReverseConnectionManager. Thread local reverse connection registry is null");
    }
    return thread_local_registry->getRCManager();
  }

private:
  static const absl::string_view RPING_MSG;
  static const absl::string_view PROXY_MSG;

  void onPingWaitTimeout();
  int fd();
  ReadOrParseState parseBuffer(Network::ListenerFilterBuffer&);

  Config config_;
  std::shared_ptr<ReverseConnection::ReverseConnRegistry> reverse_conn_registry_;

  Network::ListenerFilterCallbacks* cb_{};
  Event::FileEventPtr file_event_;

  Event::TimerPtr ping_wait_timer_;

  // Tracks whether data has been received on the connection. If the connection
  // is closed by the peer before data is received, the socket is marked dead.
  bool connection_used_;
};

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
