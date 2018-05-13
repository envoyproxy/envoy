#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include <arpa/inet.h>

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/stats.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"

#include "extensions/transport_sockets/well_known_names.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

Config::Config(Stats::Scope& scope, uint32_t max_client_hello_size)
    : stats_{ALL_TLS_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "tls_inspector."))},
      ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())),
      max_client_hello_size_(max_client_hello_size) {

  if (max_client_hello_size_ > TLS_MAX_CLIENT_HELLO) {
    throw EnvoyException(fmt::format("max_client_hello_size of {} is greater than maximum of {}.",
                                     max_client_hello_size_, size_t(TLS_MAX_CLIENT_HELLO)));
  }

  SSL_CTX_set_options(ssl_ctx_.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_tlsext_servername_callback(
      ssl_ctx_.get(), [](SSL* ssl, int* out_alert, void*) -> int {
        Filter* filter = static_cast<Filter*>(SSL_get_app_data(ssl));
        filter->onServername(SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name));

        // Return an error to stop the handshake; we have what we wanted already.
        *out_alert = SSL_AD_USER_CANCELLED;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      });
}

bssl::UniquePtr<SSL> Config::newSsl() { return bssl::UniquePtr<SSL>{SSL_new(ssl_ctx_.get())}; }

thread_local uint8_t Filter::buf_[Config::TLS_MAX_CLIENT_HELLO];

Filter::Filter(const ConfigSharedPtr config) : config_(config), ssl_(config_->newSsl()) {
  RELEASE_ASSERT(sizeof(buf_) >= config_->maxClientHelloSize());

  SSL_set_app_data(ssl_.get(), this);
  SSL_set_accept_state(ssl_.get());
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "tls inspector: new connection accepted");
  Network::ConnectionSocket& socket = cb.socket();
  ASSERT(file_event_ == nullptr);

  file_event_ = cb.dispatcher().createFileEvent(
      socket.fd(),
      [this](uint32_t events) {
        if (events & Event::FileReadyType::Closed) {
          config_->stats().connection_closed_.inc();
          done(false);
          return;
        }

        ASSERT(events == Event::FileReadyType::Read);
        onRead();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Closed);

  // TODO(PiotrSikora): make this configurable.
  timer_ = cb.dispatcher().createTimer([this]() -> void { onTimeout(); });
  timer_->enableTimer(std::chrono::milliseconds(15000));

  // TODO(ggreenway): Move timeout and close-detection to the filter manager
  // so that it applies to all listener filters.

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

void Filter::onServername(absl::string_view name) {
  if (!name.empty()) {
    config_->stats().sni_found_.inc();
    cb_->socket().setRequestedServerName(name);
  } else {
    config_->stats().sni_not_found_.inc();
  }
  clienthello_success_ = true;
}

void Filter::onRead() {
  // This receive code is somewhat complicated, because it must be done as a MSG_PEEK because
  // there is no way for a listener-filter to pass payload data to the ConnectionImpl and filters
  // that get created later.
  //
  // The file_event_ in this class gets events everytime new data is available on the socket,
  // even if previous data has not been read, which is always the case due to MSG_PEEK. When
  // the TlsInspector completes and passes the socket along, a new FileEvent is created for the
  // socket, so that new event is immediately signalled as readable because it is new and the socket
  // is readable, even though no new events have ocurred.
  //
  // TODO(ggreenway): write an integration test to ensure the events work as expected on all
  // platforms.
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  ssize_t n = os_syscalls.recv(cb_->socket().fd(), buf_, config_->maxClientHelloSize(), MSG_PEEK);
  ENVOY_LOG(trace, "tls inspector: recv: {}", n);

  if (n == -1 && errno == EAGAIN) {
    return;
  } else if (n < 0) {
    config_->stats().read_error_.inc();
    done(false);
    return;
  }

  // Because we're doing a MSG_PEEK, data we've seen before gets returned every time, so
  // skip over what we've already processed.
  if (static_cast<uint64_t>(n) > read_) {
    const uint8_t* data = buf_ + read_;
    const size_t len = n - read_;
    read_ = n;
    parseClientHello(data, len);
  }
}

void Filter::onTimeout() {
  ENVOY_LOG(trace, "tls inspector: timeout");
  config_->stats().read_timeout_.inc();
  done(false);
}

void Filter::done(bool success) {
  ENVOY_LOG(trace, "tls inspector: done: {}", success);
  timer_.reset();
  file_event_.reset();
  cb_->continueFilterChain(success);
}

void Filter::parseClientHello(const void* data, size_t len) {
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
      cb_->socket().setDetectedTransportProtocol(TransportSockets::TransportSocketNames::get().SSL);
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

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
