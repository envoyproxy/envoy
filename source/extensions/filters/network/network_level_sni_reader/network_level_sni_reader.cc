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

thread_local uint8_t NetworkLevelSniReaderFilter::buf_
    [Extensions::ListenerFilters::TlsInspector::Config::TLS_MAX_CLIENT_HELLO];

NetworkLevelSniReaderFilter::NetworkLevelSniReaderFilter(
    const Extensions::ListenerFilters::TlsInspector::ConfigSharedPtr config)
    : config_(config), ssl_(config_->newSsl()) {
  Extensions::ListenerFilters::TlsInspector::Filter::initializeSsl(
      config->maxClientHelloSize(), sizeof(buf_), ssl_,
      static_cast<Extensions::ListenerFilters::TlsInspector::TlsFilterBase*>(this));
}

Network::FilterStatus NetworkLevelSniReaderFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_CONN_LOG(trace, "NetworkLevelSniReader: got {} bytes", read_callbacks_->connection(),
                 data.length());
  if (done_) {
    return Network::FilterStatus::Continue;
  }
  // TODO: append data to the buffer instead of overwriting it.
  size_t len = (data.length() < config_->maxClientHelloSize()) ? data.length()
                                                               : config_->maxClientHelloSize();
  data.copyOut(0, len, buf_);

  Extensions::ListenerFilters::TlsInspector::Filter::parseClientHello(
      buf_, len, ssl_, read_, config_->maxClientHelloSize(), config_->stats(),
      [&](bool success) -> void { done(success); }, alpn_found_, clienthello_success_,
      []() -> void {});
  return done_ ? Network::FilterStatus::Continue : Network::FilterStatus::StopIteration;
}

void NetworkLevelSniReaderFilter::onServername(absl::string_view servername) {
  ENVOY_CONN_LOG(debug, "network level sni reader: servername: {}", read_callbacks_->connection(),
                 servername);
  Extensions::ListenerFilters::TlsInspector::Filter::doOnServername(
      servername, config_->stats(),
      [&](absl::string_view name) -> void {
        read_callbacks_->networkLevelRequestedServerName(name);
      },
      clienthello_success_);
}

void NetworkLevelSniReaderFilter::done(bool success) {
  ENVOY_LOG(trace, "network level sni reader: done: {}", success);
  done_ = true;
  if (success) {
    read_callbacks_->continueReading();
  }
}

} // namespace NetworkLevelSniReader
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
