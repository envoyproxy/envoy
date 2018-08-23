#include "extensions/filters/network/network_level_sni_reader/network_level_sni_reader.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace NetworkLevelSniReader {

Config::Config(Stats::Scope& scope, uint32_t max_client_hello_size)
    : stats_{ALL_NETWORK_LEVEL_SNI_READER_STATS(POOL_COUNTER_PREFIX(scope, "network_levelsni_reader."))},
      ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())),
      max_client_hello_size_(max_client_hello_size) {

  if (max_client_hello_size_ > TLS_MAX_CLIENT_HELLO) {
    throw EnvoyException(fmt::format("max_client_hello_size of {} is greater than maximum of {}.",
                                     max_client_hello_size_, size_t(TLS_MAX_CLIENT_HELLO)));
  }

  SSL_CTX_set_options(ssl_ctx_.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_OFF);
  // TODO: Remove following
//  SSL_CTX_set_select_certificate_cb(
//      ssl_ctx_.get(), [](const SSL_CLIENT_HELLO* client_hello) -> ssl_select_cert_result_t {
//        const uint8_t* data;
//        size_t len;
//        if (SSL_early_callback_ctx_extension_get(
//                client_hello, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &len)) {
//          NetworkLevelSniReaderFilter* filter = static_cast<NetworkLevelSniReaderFilter*>(SSL_get_app_data(client_hello->ssl));
////          filter->onALPN(data, len);
//          if (filter != nullptr) {}
//
//        }
//        return ssl_select_cert_success;
//      });
  SSL_CTX_set_tlsext_servername_callback(
      ssl_ctx_.get(), [](SSL* ssl, int* out_alert, void*) -> int {
        NetworkLevelSniReaderFilter* filter = static_cast<NetworkLevelSniReaderFilter*>(SSL_get_app_data(ssl));
        filter->onServername(SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name));
          if (filter != nullptr) {}

        // Return an error to stop the handshake; we have what we wanted already.
        *out_alert = SSL_AD_USER_CANCELLED;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      });
}

bssl::UniquePtr<SSL> Config::newSsl() { return bssl::UniquePtr<SSL>{SSL_new(ssl_ctx_.get())}; }

thread_local uint8_t NetworkLevelSniReaderFilter::buf_[Config::TLS_MAX_CLIENT_HELLO];

NetworkLevelSniReaderFilter::NetworkLevelSniReaderFilter(const ConfigSharedPtr config) : config_(config), ssl_(config_->newSsl()) {
  RELEASE_ASSERT(sizeof(buf_) >= config_->maxClientHelloSize(), "");

  SSL_set_app_data(ssl_.get(), this);
  SSL_set_accept_state(ssl_.get());
}

Network::FilterStatus NetworkLevelSniReaderFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "NetworkLevelSniReader: got {} bytes", read_callbacks_->connection(), data.length());

  // TODO: append data to the buffer instead of overwriting it.
  size_t len = (data.length() < Config::TLS_MAX_CLIENT_HELLO) ? data.length() : Config::TLS_MAX_CLIENT_HELLO ;
  data.copyOut(0, len, buf_);

  parseClientHello(buf_, len);

  return Network::FilterStatus::Continue;
}

void NetworkLevelSniReaderFilter::onServername(absl::string_view name) {
  ENVOY_CONN_LOG(debug, "network level sni reader: servername: {}", read_callbacks_->connection(), name);
  if (!name.empty()) {
    config_->stats().sni_found_.inc();
    read_callbacks_->networkLevelRequestedServerName(name);
  } else {
    config_->stats().sni_not_found_.inc();
  }
  clienthello_success_ = true;
}

void NetworkLevelSniReaderFilter::parseClientHello(const void* data, size_t len) {
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
      done(false);
    }
    break;
  case SSL_ERROR_SSL:
    if (clienthello_success_) {
      config_->stats().tls_found_.inc();
      if (alpn_found_) {
        config_->stats().alpn_found_.inc();
      } else {
        config_->stats().alpn_not_found_.inc();
      }
    } else {
      config_->stats().tls_not_found_.inc();
    }
    done(true);
    break;
  default:
    done(false);
    break;
  }
}

void NetworkLevelSniReaderFilter::done(bool success) {
  ENVOY_LOG(trace, "network level sni reader: done: {}", success);
  read_callbacks_->continueReading();
}

} // namespace NetworkLevelSniReader
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
