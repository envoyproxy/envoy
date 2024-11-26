#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/listener/reverse_connection/config.h"

#include "absl/strings/string_view.h"

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

private:
  static const absl::string_view RPING_MSG;
  static const absl::string_view PROXY_MSG;

  void onPingWaitTimeout();
  int fd();
  ReadOrParseState parseBuffer(Network::ListenerFilterBuffer&);

  Config config_;

  Network::ListenerFilterCallbacks* cb_{};
  Event::FileEventPtr file_event_;

  Event::TimerPtr ping_wait_timer_;
};

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
