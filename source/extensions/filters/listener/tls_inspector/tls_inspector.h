#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
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
#define TLS_STATS(COUNTER)                                                                         \
  COUNTER(connection_closed)                                                                       \
  COUNTER(client_hello_too_large)                                                                  \
  COUNTER(read_error)                                                                              \
  COUNTER(read_timeout)                                                                            \
  COUNTER(tls_found)                                                                               \
  COUNTER(tls_not_found)                                                                           \
  COUNTER(alpn_found)                                                                              \
  COUNTER(alpn_not_found)                                                                          \
  COUNTER(sni_found)                                                                               \
  COUNTER(sni_not_found)

/**
 * Definition of stats for the TLS. @see stats_macros.h
 */
struct TlsStats {
  TLS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for TLS inspector.
 */
class Config {
public:
  Config(Stats::Scope& scope, uint32_t max_client_hello_size = TLS_MAX_CLIENT_HELLO,
         const std::string& stat_prefix = "tls_inspector.");

  const TlsStats& stats() const { return stats_; }
  bssl::UniquePtr<SSL> newSsl();
  uint32_t maxClientHelloSize() const { return max_client_hello_size_; }

  static constexpr size_t TLS_MAX_CLIENT_HELLO = 64 * 1024;

private:
  TlsStats stats_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  const uint32_t max_client_hello_size_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

class TlsFilterBase {
public:
  virtual ~TlsFilterBase() {}

private:
  virtual void onALPN(const unsigned char* data, unsigned int len) PURE;
  virtual void onServername(absl::string_view name) PURE;

  // Allows callbacks on the SSL_CTX to set fields in this class.
  friend class Config;
};

/**
 * TLS inspector listener filter.
 */
class Filter : public Network::ListenerFilter,
               public TlsFilterBase,
               Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr config);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  static void initializeSsl(uint32_t maxClientHelloSize, size_t bufSize,
                            const bssl::UniquePtr<SSL>& ssl, void* appData);
  static void parseClientHello(const void* data, size_t len, bssl::UniquePtr<SSL>& ssl,
                               uint64_t read, uint32_t maxClientHelloSize, const TlsStats& stats,
                               std::function<void(bool)> done, bool& alpn_found,
                               bool& clienthello_success, std::function<void()> onSuccess);
  static void doOnServername(absl::string_view name, const TlsStats& stats,
                             std::function<void(absl::string_view name)> onServernameCb,
                             bool& clienthello_success_);
  static void doOnALPN(const unsigned char* data, unsigned int len,
                       std::function<void(std::vector<absl::string_view> protocols)> onAlpnCb,
                       bool& alpn_found);

private:
  void onRead();
  void onTimeout();
  void done(bool success);
  // Extensions::ListenerFilters::TlsInspector::TlsFilterBase
  void onALPN(const unsigned char* data, unsigned int len) override;
  void onServername(absl::string_view name) override;

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_;
  Event::FileEventPtr file_event_;
  Event::TimerPtr timer_;

  bssl::UniquePtr<SSL> ssl_;
  uint64_t read_{0};
  bool alpn_found_{false};
  bool clienthello_success_{false};

  static thread_local uint8_t buf_[Config::TLS_MAX_CLIENT_HELLO];
};

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
