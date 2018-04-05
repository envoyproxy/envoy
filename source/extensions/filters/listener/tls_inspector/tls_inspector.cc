#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/config/well_known_names.h"

#include "openssl/bytestring.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

Config::Config(Stats::Scope& scope)
    : stats_{ALL_TLS_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "tls_inspector."))} {}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "tls inspector: new connection accepted");
  Network::ConnectionSocket& socket = cb.socket();
  ASSERT(file_event_.get() == nullptr);
  file_event_ =
      cb.dispatcher().createFileEvent(socket.fd(),
                                      [this](uint32_t events) {
                                        ASSERT(events == Event::FileReadyType::Read);
                                        UNREFERENCED_PARAMETER(events);
                                        onRead();
                                      },
                                      Event::FileTriggerType::Edge, Event::FileReadyType::Read);

  buf_.reserve(TLS_RECORD_HEADER_SIZE + TLS_HANDSHAKE_HEADER_SIZE +
               TLS_CLIENT_HELLO_WITH_PADDING_MIN_SIZE);

  // TODO(PiotrSikora): make this configurable.
  timer_ = cb.dispatcher().createTimer([this]() -> void { onTimeout(); });
  timer_->enableTimer(std::chrono::milliseconds(15000));

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

void Filter::onRead() {
  try {
    if (peek()) {
      done(true);
    }
  } catch (const EnvoyException& ee) {
    done(false);
  }
}

void Filter::onTimeout() {
  ENVOY_LOG(trace, "tls inspector: timeout");
  config_->stats_.read_timeout_.inc();
  done(false);
}

void Filter::done(bool success) {
  ENVOY_LOG(trace, "tls inspector: done: {}", success);
  if (timer_) {
    timer_->disableTimer();
    timer_.reset();
  }
  file_event_.reset();
  cb_->continueFilterChain(success);
}

bool Filter::peek() {
  Network::ConnectionSocket& socket = cb_->socket();

  ssize_t n = recv(socket.fd(), buf_.data(), buf_.capacity(), MSG_PEEK);
  ENVOY_LOG(trace, "tls inspector: recv: {}", n);

  if (n == -1 && errno == EAGAIN) {
    return false; // Need more data.
  } else if (n == 0) {
    config_->stats_.connection_closed_.inc();
    throw EnvoyException("tls inspector: connection closed by peer");
  } else if (n < 1) {
    config_->stats_.read_error_.inc();
    throw EnvoyException("tls inspector: read error");
  } else if (client_hello_wire_size_ && n < static_cast<ssize_t>(client_hello_wire_size_)) {
    return false; // Need more data.
  }

  CBS wire;
  CBS_init(&wire, reinterpret_cast<const uint8_t*>(buf_.data()), static_cast<size_t>(n));

  // struct {
  //   ContentType type;
  //   ProtocolVersion version;
  //   uint16 length;
  //   opaque fragment[TLSPlaintext.length];
  // } TLSPlaintext;

  // Check TLSPlaintext.type.
  uint8_t u8;
  if (!CBS_get_u8(&wire, &u8)) {
    return false; // Need more data.
  } else if (u8 != TLS_RECORD_HANDSHAKE) {
    ENVOY_LOG(debug, "tls inspector: not a TLS connection (record type mismatch)");
    socket.setDetectedTransportProtocol(Envoy::Config::TransportSocketNames::get().RAW_BUFFER);
    config_->stats_.found_raw_buffer_.inc();
    return true; // Done, not TLS.
  }

  // Check TLSPlaintext.version.
  uint16_t u16;
  if (!CBS_get_u16(&wire, &u16)) {
    return false; // Need more data.
  } else if (u16 < TLS_VERSION_SSL3 && u16 > TLS_VERSION_TLS12) {
    ENVOY_LOG(debug, "tls inspector: not a TLS connection (record version mismatch)");
    socket.setDetectedTransportProtocol(Envoy::Config::TransportSocketNames::get().RAW_BUFFER);
    config_->stats_.found_raw_buffer_.inc();
    return true; // Done, not TLS.
  }

  // Read TLSPlaintext.length.
  if (!CBS_get_u16(&wire, &u16)) {
    return false; // Need more data.
  }

  // struct {
  //   HandshakeType msg_type;
  //   uint24 length;
  //   ClientHello body;
  // } Handshake;

  // Check Handshake.type.
  if (!CBS_get_u8(&wire, &u8)) {
    return false; // Need more data.
  } else if (u8 != TLS_HANDSHAKE_CLIENT_HELLO) {
    config_->stats_.invalid_handshake_message_.inc();
    throw EnvoyException("tls inspector: invalid Hanshake message");
  }

  // Read Handshake.length.
  uint32_t u32;
  if (!CBS_get_u24(&wire, &u32)) {
    return false; // Need more data.
  }

  // Reject unreasonably big ClientHello messages.
  if (u32 > 65535) {
    config_->stats_.invalid_fragment_length_.inc();
    throw EnvoyException(fmt::format("tls inspector: invalid ClientHello length: {}", u32));
  }

  client_hello_wire_size_ = TLS_RECORD_HEADER_SIZE + TLS_HANDSHAKE_HEADER_SIZE + u32;

  if (client_hello_wire_size_ > buf_.capacity()) {
    buf_.reserve(client_hello_wire_size_);
    if (n < static_cast<ssize_t>(client_hello_wire_size_)) {
      return false; // Need more data.
    } else {
      return peek(); // Re-read into bigger buffer.
    }
  }

  // TODO(PiotrSikora): support ClientHello spanning multiple TLS record fragments... maybe?
  if (CBS_len(&wire) != u32) {
    config_->stats_.invalid_fragment_length_.inc();
    throw EnvoyException(fmt::format("tls inspector: invalid fragment length: {}", u32));
  }

  processTlsClientHello(&wire);
  return true;
}

void Filter::processTlsClientHello(CBS* client_hello) {
  // struct {
  //   ProtocolVersion client_version;
  //   Random random;
  //   SessionID session_id;
  //   CipherSuite cipher_suites<2..2^16-1>;
  //   CompressionMethod compression_methods<1..2^8-1>;
  //   Extension extensions<0..2^16-1>;
  // } ClientHello;

  uint8_t u8;
  uint16_t u16, version;

  // Read ClientHello.client_version.
  if (!CBS_get_u16(client_hello, &version) ||
      // Skip ClientHello.random.
      !CBS_skip(client_hello, TLS_CLIENT_HELLO_RANDOM_SIZE) ||
      // Skip ClientHello.session_id.
      !CBS_get_u8(client_hello, &u8) || u8 > TLS_CLIENT_HELLO_SESSION_ID_MAX_SIZE ||
      !CBS_skip(client_hello, u8) ||
      // Skip ClientHello.cipher_suites.
      !CBS_get_u16(client_hello, &u16) || u16 < 2 || (u16 & 1) != 0 ||
      !CBS_skip(client_hello, u16) ||
      // Skip ClientHello.compression_methods.
      !CBS_get_u8(client_hello, &u8) || u8 < 1 || !CBS_skip(client_hello, u8)) {
    config_->stats_.invalid_client_hello_.inc();
    throw EnvoyException("tls inspector: failed to process ClientHello");
  }

  std::string server_name;
  std::vector<std::string> next_protocols;

  // Read ClientHello.extensions, if any.
  if (CBS_len(client_hello) != 0) {
    CBS extensions;
    if (!CBS_get_u16_length_prefixed(client_hello, &extensions) || CBS_len(client_hello) != 0) {
      config_->stats_.invalid_client_hello_.inc();
      throw EnvoyException("tls inspector: failed to process ClientHello extensions");
    }

    while (CBS_len(&extensions) > 0) {
      CBS extension;
      if (!CBS_get_u16(&extensions, &u16) ||
          !CBS_get_u16_length_prefixed(&extensions, &extension)) {
        config_->stats_.invalid_client_hello_.inc();
        throw EnvoyException("tls inspector: failed to process ClientHello extension");
      }

      switch (u16) {
      case TLS_EXTENSION_SNI: {
        CBS server_names, name;
        if (!CBS_get_u16_length_prefixed(&extension, &server_names) || CBS_len(&extension) != 0 ||
            !CBS_get_u8(&server_names, &u8) || u8 != TLS_EXTENSION_SNI_HOSTNAME ||
            !CBS_get_u16_length_prefixed(&server_names, &name) || CBS_len(&name) == 0 ||
            CBS_len(&name) > TLS_EXTENSION_SNI_HOSTNAME_MAX_SIZE || CBS_contains_zero_byte(&name) ||
            CBS_len(&server_names) != 0) {
          config_->stats_.invalid_client_hello_.inc();
          throw EnvoyException("tls inspector: failed to process SNI extension");
        }
        server_name.assign(reinterpret_cast<const char*>(CBS_data(&name)), CBS_len(&name));
      } break;

      case TLS_EXTENSION_ALPN: {
        CBS protocols;
        if (!CBS_get_u16_length_prefixed(&extension, &protocols) || CBS_len(&extension) != 0 ||
            CBS_len(&protocols) < 2) {
          config_->stats_.invalid_client_hello_.inc();
          throw EnvoyException("tls inspector: failed to process ALPN extension");
        }
        while (CBS_len(&protocols) > 0) {
          CBS name;
          if (!CBS_get_u8_length_prefixed(&protocols, &name) || CBS_len(&name) == 0) {
            config_->stats_.invalid_client_hello_.inc();
            throw EnvoyException("tls inspector: failed to process ALPN extension");
          }
          const char* data = reinterpret_cast<const char*>(CBS_data(&name));
          size_t len = CBS_len(&name);
          if (!isTlsGreaseValue(data, len)) {
            next_protocols.emplace_back(data, len);
          }
        }
      } break;

      case TLS_EXTENSION_SUPPORTED_VERSIONS: {
        CBS versions;
        if (!CBS_get_u8_length_prefixed(&extension, &versions) || CBS_len(&extension) != 0 ||
            CBS_len(&versions) == 0) {
          config_->stats_.invalid_client_hello_.inc();
          throw EnvoyException("tls inspector: failed to process supported versions extension");
        }
        while (CBS_len(&versions) > 0) {
          // Override ClientHello.client_version with first (the most preferred) supported version.
          CBS_get_u16(&versions, &u16);
          if (!isTlsGreaseValue(u16)) {
            version = u16;
            break;
          }
        }
      } break;
      }
    }
  }

  switch (version) {
  case TLS_VERSION_SSL3:
    ENVOY_LOG(debug, "tls inspector: found SSLv3");
    config_->stats_.found_ssl_v3_.inc();
    break;
  case TLS_VERSION_TLS10:
    ENVOY_LOG(debug, "tls inspector: found TLSv1.0");
    config_->stats_.found_tls_v1_0_.inc();
    break;
  case TLS_VERSION_TLS11:
    ENVOY_LOG(debug, "tls inspector: found TLSv1.1");
    config_->stats_.found_tls_v1_1_.inc();
    break;
  case TLS_VERSION_TLS12:
    ENVOY_LOG(debug, "tls inspector: found TLSv1.2");
    config_->stats_.found_tls_v1_2_.inc();
    break;
  case TLS_VERSION_TLS13:
  case TLS_VERSION_TLS13_DRAFT23:
  case TLS_VERSION_TLS13_DRAFT28:
    ENVOY_LOG(debug, "tls inspector: found TLSv1.3");
    config_->stats_.found_tls_v1_3_.inc();
    break;
  default:
    ENVOY_LOG(debug, "tls inspector: found unknown version of TLS: {}", version);
    config_->stats_.found_tls_unknown_version_.inc();
    break;
  }

  Network::ConnectionSocket& socket = cb_->socket();
  socket.setDetectedTransportProtocol(Envoy::Config::TransportSocketNames::get().SSL);

  if (!server_name.empty()) {
    ENVOY_LOG(debug, "tls inspector: found server name: {}", server_name);
    config_->stats_.found_tls_extension_sni_.inc();
    socket.setRequestedServerName(server_name);
  }

  if (!next_protocols.empty()) {
    for (const auto& item : next_protocols) {
      ENVOY_LOG(debug, "tls inspector: found ALPN: {}", item);
    }
    socket.setRequestedNextProtocol(next_protocols);
    config_->stats_.found_tls_extension_alpn_.inc();
  }
}

bool Filter::isTlsGreaseValue(uint16_t u16) {
  if ((u16 & 0x0f0f) == 0x0a0a && ((u16 >> 8) & 0xf0) == (u16 & 0xf0)) {
    return true;
  }
  return false;
}

bool Filter::isTlsGreaseValue(const char* data, size_t len) {
  if (len >= sizeof("ignore/") - 1 &&
      strncmp(data, static_cast<const char*>("ignore/"), sizeof("ignore/") - 1) == 0) {
    return true;
  }
  return false;
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
