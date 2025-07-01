#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"

#include "absl/strings/string_view.h"

// Configuration header for reverse connection listener filter
#include "source/extensions/filters/listener/reverse_connection/config.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

enum class ReadOrParseState { Done, TryAgainLater, Error };

/**
 * Listener filter to store reverse connections until they are actively used.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const Config& config);
  ~Filter();

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  size_t maxReadBytes() const override;
  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override;
  void onClose() override;

  // Helper method to get file descriptor
  int fd();

private:
  // RPING/PROXY messages now handled by ReverseConnectionUtility

  void onPingWaitTimeout();
  ReadOrParseState parseBuffer(Network::ListenerFilterBuffer&);

  Config config_;

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
