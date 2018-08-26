#pragma once

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/ssl/utility.h"

#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace NetworkLevelSniReader {

/**
 * Global configuration for network level SNI reader.
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

class NetworkLevelSniReaderFilter : public Network::ReadFilter,
                                    Logger::Loggable<Logger::Id::filter> {
public:
  NetworkLevelSniReaderFilter(const ConfigSharedPtr config);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  void done(bool success);
  void onServername(absl::string_view name);

  ConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};

  bssl::UniquePtr<SSL> ssl_;
  uint64_t read_{0};
  bool alpn_found_{false};
  bool clienthello_success_{false};
  bool done_{false};

  static thread_local uint8_t buf_[Config::TLS_MAX_CLIENT_HELLO];

  // Allows callbacks on the SSL_CTX to set fields in this class.
  friend class Config;
};

} // namespace NetworkLevelSniReader
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
