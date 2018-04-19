#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

/**
 * All stats for the TLS inspector. @see stats_macros.h
 */
#define ALL_TLS_INSPECTOR_STATS(COUNTER)                                                           \
  COUNTER(connection_closed)                                                                       \
  COUNTER(client_hello_too_big)                                                                    \
  COUNTER(invalid_client_hello)                                                                    \
  COUNTER(read_error)                                                                              \
  COUNTER(read_timeout)                                                                            \
  COUNTER(no_sni_found)                                                                            \
  COUNTER(sni_found)

/**
 * Definition of all stats for the TLS inspector. @see stats_macros.h
 */
struct TlsInspectorStats {
  ALL_TLS_INSPECTOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for TLS inspector.
 */
class Config {
public:
  Config(Stats::Scope& scope);

  const TlsInspectorStats& stats() const { return stats_; }
  bssl::UniquePtr<SSL> newSsl();

private:
  TlsInspectorStats stats_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * TLS inspector listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr config);
  Filter(const ConfigSharedPtr config, uint32_t max_client_hello_size);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  static const size_t TLS_DEFAULT_MAX_CLIENT_HELLO = 64 * 1024;

  void parseClientHello(const void* data, size_t len);
  void onRead();
  void onTimeout();
  void done(bool success);
  void onServername(absl::string_view name);

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_;
  Event::FileEventPtr file_event_;
  Event::TimerPtr timer_;

  bssl::UniquePtr<SSL> ssl_;
  std::vector<uint8_t> buf_;
  uint64_t read_{0};
  bool clienthello_success_{false};

  // Allows callbacks on the SSL_CTX to set fields in this class.
  friend class Config;
};

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
