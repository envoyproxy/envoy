#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/hex.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "openssl/md5.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

// Min/max TLS version recognized by the underlying TLS/SSL library.
const unsigned Config::TLS_MIN_SUPPORTED_VERSION = TLS1_VERSION;
const unsigned Config::TLS_MAX_SUPPORTED_VERSION = TLS1_3_VERSION;

Config::Config(
    Stats::Scope& scope,
    const envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector& proto_config,
    uint32_t max_client_hello_size)
    : stats_{ALL_TLS_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "tls_inspector."))},
      ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())),
      enable_ja3_fingerprinting_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, enable_ja3_fingerprinting, false)),
      max_client_hello_size_(max_client_hello_size) {

  if (max_client_hello_size_ > TLS_MAX_CLIENT_HELLO) {
    throw EnvoyException(fmt::format("max_client_hello_size of {} is greater than maximum of {}.",
                                     max_client_hello_size_, size_t(TLS_MAX_CLIENT_HELLO)));
  }

  SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS_MIN_SUPPORTED_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS_MAX_SUPPORTED_VERSION);
  SSL_CTX_set_options(ssl_ctx_.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_select_certificate_cb(
      ssl_ctx_.get(), [](const SSL_CLIENT_HELLO* client_hello) -> ssl_select_cert_result_t {
        Filter* filter = static_cast<Filter*>(SSL_get_app_data(client_hello->ssl));
        filter->createJA3Hash(client_hello);

        const uint8_t* data;
        size_t len;
        if (SSL_early_callback_ctx_extension_get(
                client_hello, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &len)) {
          filter->onALPN(data, len);
        }
        return ssl_select_cert_success;
      });
  SSL_CTX_set_tlsext_servername_callback(
      ssl_ctx_.get(), [](SSL* ssl, int* out_alert, void*) -> int {
        Filter* filter = static_cast<Filter*>(SSL_get_app_data(ssl));
        filter->onServername(
            absl::NullSafeStringView(SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name)));

        // Return an error to stop the handshake; we have what we wanted already.
        *out_alert = SSL_AD_USER_CANCELLED;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      });
}

bssl::UniquePtr<SSL> Config::newSsl() { return bssl::UniquePtr<SSL>{SSL_new(ssl_ctx_.get())}; }

Filter::Filter(const ConfigSharedPtr& config) : config_(config), ssl_(config_->newSsl()) {
  SSL_set_app_data(ssl_.get(), this);
  SSL_set_accept_state(ssl_.get());
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "tls inspector: new connection accepted");
  cb_ = &cb;

  return Network::FilterStatus::StopIteration;
}

void Filter::onALPN(const unsigned char* data, unsigned int len) {
  CBS wire, list;
  CBS_init(&wire, reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(len));
  if (!CBS_get_u16_length_prefixed(&wire, &list) || CBS_len(&wire) != 0 || CBS_len(&list) < 2) {
    // Don't produce errors, let the real TLS stack do it.
    return;
  }
  CBS name;
  std::vector<absl::string_view> protocols;
  while (CBS_len(&list) > 0) {
    if (!CBS_get_u8_length_prefixed(&list, &name) || CBS_len(&name) == 0) {
      // Don't produce errors, let the real TLS stack do it.
      return;
    }
    protocols.emplace_back(reinterpret_cast<const char*>(CBS_data(&name)), CBS_len(&name));
  }
  ENVOY_LOG(trace, "tls:onALPN(), ALPN: {}", absl::StrJoin(protocols, ","));
  cb_->socket().setRequestedApplicationProtocols(protocols);
  alpn_found_ = true;
}

void Filter::onServername(absl::string_view name) {
  if (!name.empty()) {
    config_->stats().sni_found_.inc();
    cb_->socket().setRequestedServerName(name);
    ENVOY_LOG(debug, "tls:onServerName(), requestedServerName: {}", name);
  } else {
    config_->stats().sni_not_found_.inc();
  }
  clienthello_success_ = true;
}

Network::FilterStatus Filter::onData(Network::ListenerFilterBuffer& buffer) {
  auto raw_slice = buffer.rawSlice();
  ENVOY_LOG(trace, "tls inspector: recv: {}", raw_slice.len_);

  // Because we're doing a MSG_PEEK, data we've seen before gets returned every time, so
  // skip over what we've already processed.
  if (static_cast<uint64_t>(raw_slice.len_) > read_) {
    const uint8_t* data = static_cast<const uint8_t*>(raw_slice.mem_) + read_;
    const size_t len = raw_slice.len_ - read_;
    read_ = raw_slice.len_;
    ParseState parse_state = parseClientHello(data, len);
    switch (parse_state) {
    case ParseState::Error:
      cb_->socket().ioHandle().close();
      return Network::FilterStatus::StopIteration;
    case ParseState::Done:
      // Finish the inspect.
      return Network::FilterStatus::Continue;
    case ParseState::Continue:
      // Do nothing but wait for the next event.
      return Network::FilterStatus::StopIteration;
    }
    IS_ENVOY_BUG("unexpected tcp filter parse_state");
  }
  return Network::FilterStatus::StopIteration;
}

ParseState Filter::parseClientHello(const void* data, size_t len) {
  // Ownership is passed to ssl_ in SSL_set_bio()
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(data, len));

  // Make the mem-BIO return that there is more data
  // available beyond it's end
  BIO_set_mem_eof_return(bio.get(), -1);

  SSL_set_bio(ssl_.get(), bio.get(), bio.get());
  bio.release();

  int ret = SSL_do_handshake(ssl_.get());

  // This should never succeed because an error is always returned from the SNI callback.
  ASSERT(ret <= 0);
  switch (SSL_get_error(ssl_.get(), ret)) {
  case SSL_ERROR_WANT_READ:
    if (read_ == config_->maxClientHelloSize()) {
      // We've hit the specified size limit. This is an unreasonably large ClientHello;
      // indicate failure.
      config_->stats().client_hello_too_large_.inc();
      return ParseState::Error;
    }
    return ParseState::Continue;
  case SSL_ERROR_SSL:
    if (clienthello_success_) {
      config_->stats().tls_found_.inc();
      if (alpn_found_) {
        config_->stats().alpn_found_.inc();
      } else {
        config_->stats().alpn_not_found_.inc();
      }
      cb_->socket().setDetectedTransportProtocol("tls");
    } else {
      config_->stats().tls_not_found_.inc();
    }
    return ParseState::Done;
  default:
    return ParseState::Error;
  }
}

// Google GREASE values (https://datatracker.ietf.org/doc/html/rfc8701)
static constexpr std::array<uint16_t, 16> GREASE = {
    0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
    0x8a8a, 0x9a9a, 0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa,
};

bool isNotGrease(uint16_t id) {
  return std::find(GREASE.begin(), GREASE.end(), id) == GREASE.end();
}

void writeCipherSuites(const SSL_CLIENT_HELLO* ssl_client_hello, std::string& fingerprint) {
  CBS cipher_suites;
  CBS_init(&cipher_suites, ssl_client_hello->cipher_suites, ssl_client_hello->cipher_suites_len);

  bool write_cipher = true;
  bool first = true;
  while (write_cipher && CBS_len(&cipher_suites) > 0) {
    uint16_t id;
    write_cipher = CBS_get_u16(&cipher_suites, &id);
    if (write_cipher && isNotGrease(id)) {
      if (!first) {
        absl::StrAppend(&fingerprint, "-");
      }
      absl::StrAppendFormat(&fingerprint, "%d", id);
      first = false;
    }
  }
}

void writeExtensions(const SSL_CLIENT_HELLO* ssl_client_hello, std::string& fingerprint) {
  CBS extensions;
  CBS_init(&extensions, ssl_client_hello->extensions, ssl_client_hello->extensions_len);

  bool write_extension = true;
  bool first = true;
  while (write_extension && CBS_len(&extensions) > 0) {
    uint16_t id;
    CBS extension;

    write_extension =
        (CBS_get_u16(&extensions, &id) && CBS_get_u16_length_prefixed(&extensions, &extension));
    if (write_extension && isNotGrease(id)) {
      if (!first) {
        absl::StrAppend(&fingerprint, "-");
      }
      absl::StrAppendFormat(&fingerprint, "%d", id);
      first = false;
    }
  }
}

void writeEllipticCurves(const SSL_CLIENT_HELLO* ssl_client_hello, std::string& fingerprint) {
  const uint8_t* ec_data;
  size_t ec_len;
  if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_supported_groups, &ec_data,
                                           &ec_len)) {
    CBS ec;
    CBS_init(&ec, ec_data, ec_len);

    // skip list length
    uint16_t id;
    bool write_elliptic_curve = CBS_get_u16(&ec, &id);

    bool first = true;
    while (write_elliptic_curve && CBS_len(&ec) > 0) {
      write_elliptic_curve = CBS_get_u16(&ec, &id);
      if (write_elliptic_curve) {
        if (!first) {
          absl::StrAppend(&fingerprint, "-");
        }
        absl::StrAppendFormat(&fingerprint, "%d", id);
        first = false;
      }
    }
  }
}

void writeEllipticCurvePointFormats(const SSL_CLIENT_HELLO* ssl_client_hello,
                                    std::string& fingerprint) {
  const uint8_t* ecpf_data;
  size_t ecpf_len;
  if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_ec_point_formats,
                                           &ecpf_data, &ecpf_len)) {
    CBS ecpf;
    CBS_init(&ecpf, ecpf_data, ecpf_len);

    // skip list length
    uint8_t id;
    bool write_point_format = CBS_get_u8(&ecpf, &id);

    bool first = true;
    while (write_point_format && CBS_len(&ecpf) > 0) {
      write_point_format = CBS_get_u8(&ecpf, &id);
      if (write_point_format) {
        if (!first) {
          absl::StrAppend(&fingerprint, "-");
        }
        absl::StrAppendFormat(&fingerprint, "%d", id);
        first = false;
      }
    }
  }
}

void Filter::createJA3Hash(const SSL_CLIENT_HELLO* ssl_client_hello) {
  if (config_->enableJA3Fingerprinting()) {
    std::string fingerprint;
    const uint16_t client_version = ssl_client_hello->version;
    absl::StrAppendFormat(&fingerprint, "%d,", client_version);
    writeCipherSuites(ssl_client_hello, fingerprint);
    absl::StrAppend(&fingerprint, ",");
    writeExtensions(ssl_client_hello, fingerprint);
    absl::StrAppend(&fingerprint, ",");
    writeEllipticCurves(ssl_client_hello, fingerprint);
    absl::StrAppend(&fingerprint, ",");
    writeEllipticCurvePointFormats(ssl_client_hello, fingerprint);

    ENVOY_LOG(trace, "tls:createJA3Hash(), fingerprint: {}", fingerprint);

    uint8_t buf[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const uint8_t*>(fingerprint.data()), fingerprint.size(), buf);
    std::string md5 = Envoy::Hex::encode(buf, MD5_DIGEST_LENGTH);
    ENVOY_LOG(trace, "tls:createJA3Hash(), hash: {}", md5);

    cb_->socket().setJA3Hash(md5);
  }
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
