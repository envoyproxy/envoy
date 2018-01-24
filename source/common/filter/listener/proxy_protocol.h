#pragma once

#include "envoy/event/file_event.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Filter {
namespace Listener {
namespace ProxyProtocol {

/**
 * All stats for the proxy protocol. @see stats_macros.h
 */
// clang-format off
#define ALL_PROXY_PROTOCOL_STATS(COUNTER)                                                          \
  COUNTER(downstream_cx_proxy_proto_error)
// clang-format on

/**
 * Definition of all stats for the proxy protocol. @see stats_macros.h
 */
struct ProxyProtocolStats {
  ALL_PROXY_PROTOCOL_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for Proxy Protocol listener filter.
 */
class Config {
public:
  Config(Stats::Scope& scope);

  ProxyProtocolStats stats_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * Implementation the PROXY Protocol V1 listener filter
 * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
 */
class Instance : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Instance(const ConfigSharedPtr& config) : config_(config) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  static const size_t MAX_PROXY_PROTO_LEN = 108;

  void onRead();
  void onReadWorker();

  /**
   * Helper function that attempts to read a line (delimited by '\r\n') from the socket.
   * throws EnvoyException on any socket errors.
   * @return bool true if a line should be read, false if more data is needed.
   */
  bool readLine(int fd, std::string& s);

  Network::ListenerFilterCallbacks* cb_{};
  Event::FileEventPtr file_event_;

  // The offset in buf_ that has been fully read
  size_t buf_off_{};

  // The index in buf_ where the search for '\r\n' should continue from
  size_t search_index_{1};

  // Stores the portion of the first line that has been read so far.
  char buf_[MAX_PROXY_PROTO_LEN];

  ConfigSharedPtr config_;
};

} // namespace ProxyProtocol
} // namespace Listener
} // namespace Filter
} // namespace Envoy
