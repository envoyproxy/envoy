#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "openssl/bytestring.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

/**
 * All stats for the TLS inspector. @see stats_macros.h
 */
#define ALL_TLS_INSPECTOR_STATS(COUNTER)                                                           \
  COUNTER(found_raw_buffer)                                                                        \
  COUNTER(found_ssl_v3)                                                                            \
  COUNTER(found_tls_v1_0)                                                                          \
  COUNTER(found_tls_v1_1)                                                                          \
  COUNTER(found_tls_v1_2)                                                                          \
  COUNTER(found_tls_v1_3)                                                                          \
  COUNTER(found_tls_unknown_version)                                                               \
  COUNTER(found_tls_extension_sni)                                                                 \
  COUNTER(found_tls_extension_alpn)                                                                \
  COUNTER(invalid_handshake_message)                                                               \
  COUNTER(invalid_fragment_length)                                                                 \
  COUNTER(invalid_client_hello)                                                                    \
  COUNTER(read_error)                                                                              \
  COUNTER(read_timeout)                                                                            \
  COUNTER(connection_closed)

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

  TlsInspectorStats stats_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * TLS inspector listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr config) : config_(config) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  static const size_t TLS_RECORD_HEADER_SIZE = 5;
  static const size_t TLS_HANDSHAKE_HEADER_SIZE = 4;
  static const size_t TLS_CLIENT_HELLO_WITH_PADDING_MIN_SIZE = 512;

  static const size_t TLS_CLIENT_HELLO_RANDOM_SIZE = 32;
  static const size_t TLS_CLIENT_HELLO_SESSION_ID_MAX_SIZE = 32;

  static const uint8_t TLS_RECORD_HANDSHAKE = 22;
  static const uint8_t TLS_HANDSHAKE_CLIENT_HELLO = 1;

  static const uint8_t TLS_EXTENSION_SNI = 0;
  static const uint8_t TLS_EXTENSION_SNI_HOSTNAME = 0;
  static const uint8_t TLS_EXTENSION_SNI_HOSTNAME_MAX_SIZE = 255;
  static const uint8_t TLS_EXTENSION_ALPN = 16;
  static const uint8_t TLS_EXTENSION_SUPPORTED_VERSIONS = 43;

  static const uint16_t TLS_VERSION_SSL3 = 0x0300;
  static const uint16_t TLS_VERSION_TLS10 = 0x0301;
  static const uint16_t TLS_VERSION_TLS11 = 0x0302;
  static const uint16_t TLS_VERSION_TLS12 = 0x0303;
  static const uint16_t TLS_VERSION_TLS13 = 0x0304;
  // TLSv1.3 drafts that shipped in browsers and/or Envoy.
  // TODO(PiotrSikora): remove at the end of 2018.
  static const uint16_t TLS_VERSION_TLS13_DRAFT23 = 0x7f17;
  static const uint16_t TLS_VERSION_TLS13_DRAFT28 = 0x7f1c;

  /**
   * @return true if input is a TLS GREASE value and should be ignored.
   *         See: https://tools.ietf.org/html/draft-ietf-tls-grease
   */
  static bool isTlsGreaseValue(uint16_t u16);
  static bool isTlsGreaseValue(const char* data, size_t len);

  void onRead();
  void onTimeout();
  void done(bool success);

  /**
   * Helper function that attempts to peek at first few bytes of the data on the wire and determine
   * whether it's dealing with TCP or TLS connection.
   * @return false if needs more data, return true if done processing.
   */
  bool peek();

  /**
   * Helper function that attempts to process TLS ClientHello message and extracts information
   * about requested hostname (from SNI) and next protocol (from ALPN).
   * @throws EnvoyException if it runs into errors trying to process invalid ClientHello message.
   */
  void processTlsClientHello(CBS* client_hello);

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_;
  Event::FileEventPtr file_event_;
  Event::TimerPtr timer_;

  std::vector<uint8_t*> buf_{};
  size_t client_hello_wire_size_{};
};

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
