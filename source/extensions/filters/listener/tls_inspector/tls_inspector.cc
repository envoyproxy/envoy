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
#include "common/config/well_known_names.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

namespace {

// This could use the same index as Ssl::ContextImpl, but allocating 1 extra
// index seems better than adding the coupling between components.
int sslIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int ssl_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(ssl_index >= 0);
    return ssl_index;
  }());
}

} // namespace

Config::Config(Stats::Scope& scope)
    : stats_{ALL_TLS_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "tls_inspector."))},
      ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())) {
  SSL_CTX_set_options(ssl_ctx_.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_tlsext_servername_callback(
      ssl_ctx_.get(), [](SSL* ssl, int* out_alert, void*) -> int {
        Filter* filter = static_cast<Filter*>(SSL_get_ex_data(ssl, sslIndex()));
        filter->onServername(SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name));

        // Return an error to stop the handshake; we have what we wanted already.
        *out_alert = SSL_AD_USER_CANCELLED;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      });
}

bssl::UniquePtr<SSL> Config::newSsl() { return bssl::UniquePtr<SSL>{SSL_new(ssl_ctx_.get())}; }

Filter::Filter(const ConfigSharedPtr config) : Filter(config, TLS_DEFAULT_MAX_CLIENT_HELLO) {}

Filter::Filter(const ConfigSharedPtr config, uint32_t max_client_hello_size)
    : config_(config), ssl_(config_->newSsl()) {
  SSL_set_ex_data(ssl_.get(), sslIndex(), this);
  SSL_set_accept_state(ssl_.get());

  // TODO(ggreenway) PERF: Put a buffer in thread-local storage and have every Filter share
  // the buffer.
  buf_.reserve(max_client_hello_size);
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "tls inspector: new connection accepted");
  Network::ConnectionSocket& socket = cb.socket();
  ASSERT(file_event_ == nullptr);

  // TODO(ggreenway): detect closed connections.
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

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

void Filter::onServername(absl::string_view name) {
  if (!name.empty()) {
    config_->stats().sni_found_.inc();
    cb_->socket().setRequestedServerName(name);
  } else {
    config_->stats().no_sni_found_.inc();
  }
  clienthello_success_ = true;

  // TODO(ggreenway): Notify the ConnectionSocket that this
  // is a TLS connection.
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
  ssize_t n = os_syscalls.recv(cb_->socket().fd(), buf_.data(), buf_.capacity(), MSG_PEEK);
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
    uint8_t* data = buf_.data() + read_;
    size_t len = n - read_;
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

void Filter::parseClientHello(void* data, size_t len) {
  // Ownership is passed to ssl_ in SSL_set_bio()
  BIO* bio = BIO_new_mem_buf(data, len);
  SSL_set_bio(ssl_.get(), bio, bio);

  // Make the mem-BIO return that there is more data
  // available beyond it's end
  BIO_set_mem_eof_return(bio, -1);

  int ret = SSL_do_handshake(ssl_.get());

  switch (SSL_get_error(ssl_.get(), ret)) {
  case SSL_ERROR_WANT_READ:
    if (read_ == buf_.capacity()) {
      // We've hit the specified size limit. This is an unreasonably large ClientHello;
      // indicate failure.
      config_->stats().client_hello_too_big_.inc();
      done(false);
    }
    break;
  case SSL_ERROR_SSL:
    if (clienthello_success_) {
      done(true);
    } else {
      config_->stats().invalid_client_hello_.inc();
      done(false);
    }
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
