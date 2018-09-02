#pragma once

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"

#include "extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace NetworkLevelSniReader {

class NetworkLevelSniReaderFilter : public Network::ReadFilter,
                                    public Extensions::ListenerFilters::TlsInspector::TlsFilterBase,
                                    Logger::Loggable<Logger::Id::filter> {
public:
  NetworkLevelSniReaderFilter(
      const Extensions::ListenerFilters::TlsInspector::ConfigSharedPtr config);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  void done(bool success);
  // Extensions::ListenerFilters::TlsInspector::TlsFilterBase
  void onServername(absl::string_view name) override;
  void onALPN(const unsigned char*, unsigned int) override{};

  Extensions::ListenerFilters::TlsInspector::ConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};

  bssl::UniquePtr<SSL> ssl_;
  uint64_t read_{0};
  bool alpn_found_{false};
  bool clienthello_success_{false};
  bool done_{false};

  static thread_local uint8_t
      buf_[Extensions::ListenerFilters::TlsInspector::Config::TLS_MAX_CLIENT_HELLO];
};

} // namespace NetworkLevelSniReader
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
