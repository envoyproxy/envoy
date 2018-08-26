#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/ssl/utility.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

/**
 * Global configuration for TLS inspector.
 */
class Config {
public:
  Config(Stats::Scope& scope, uint32_t max_client_hello_size = TLS_MAX_CLIENT_HELLO);

  const Ssl::Utility::TlsStats& stats() const { return stats_; }
  bssl::UniquePtr<SSL> newSsl();
  uint32_t maxClientHelloSize() const { return max_client_hello_size_; }

  static constexpr size_t TLS_MAX_CLIENT_HELLO = 64 * 1024;

private:
  Ssl::Utility::TlsStats stats_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  const uint32_t max_client_hello_size_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * TLS inspector listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr config);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  void parseClientHello(const void* data, size_t len);
  void onRead();
  void onTimeout();
  void done(bool success);
  void onALPN(const unsigned char* data, unsigned int len);
  void onServername(absl::string_view name);

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_;
  Event::FileEventPtr file_event_;
  Event::TimerPtr timer_;

  bssl::UniquePtr<SSL> ssl_;
  uint64_t read_{0};
  bool alpn_found_{false};
  bool clienthello_success_{false};

  static thread_local uint8_t buf_[Config::TLS_MAX_CLIENT_HELLO];

  // Allows callbacks on the SSL_CTX to set fields in this class.
  friend class Config;
};

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
