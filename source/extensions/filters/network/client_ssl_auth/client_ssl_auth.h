#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

#include "envoy/config/filter/network/client_ssl_auth/v2/client_ssl_auth.pb.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/rest_api_fetcher.h"
#include "common/network/cidr_range.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

/**
 * All client SSL auth stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CLIENT_SSL_AUTH_STATS(COUNTER, GAUGE)                                                  \
  COUNTER(update_success)                                                                          \
  COUNTER(update_failure)                                                                          \
  COUNTER(auth_no_ssl)                                                                             \
  COUNTER(auth_ip_white_list)                                                                      \
  COUNTER(auth_digest_match)                                                                       \
  COUNTER(auth_digest_no_match)                                                                    \
  GAUGE  (total_principals)
// clang-format on

/**
 * Struct definition for all client SSL auth stats. @see stats_macros.h
 */
struct GlobalStats {
  ALL_CLIENT_SSL_AUTH_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Wraps the principals currently allowed to authenticate.
 */
class AllowedPrincipals : public ThreadLocal::ThreadLocalObject {
public:
  void add(const std::string& sha256_digest) {
    if (!sha256_digest.empty()) {
      allowed_sha256_digests_.emplace(sha256_digest);
    }
  }
  bool allowed(const std::string& sha256_digest) const {
    return allowed_sha256_digests_.count(sha256_digest) != 0;
  }
  size_t size() const { return allowed_sha256_digests_.size(); }

private:
  std::unordered_set<std::string> allowed_sha256_digests_;
};

typedef std::shared_ptr<AllowedPrincipals> AllowedPrincipalsSharedPtr;

class ClientSslAuthConfig;
typedef std::shared_ptr<ClientSslAuthConfig> ClientSslAuthConfigSharedPtr;

/**
 * Global configuration for client SSL authentication. The config contacts a JSON API to fetch the
 * list of allowed principals, caches it, then makes auth decisions on it and any associated IP
 * white list.
 */
class ClientSslAuthConfig : public Http::RestApiFetcher {
public:
  static ClientSslAuthConfigSharedPtr
  create(const envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& config,
         ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm,
         Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::RandomGenerator& random);

  const AllowedPrincipals& allowedPrincipals();
  const Network::Address::IpList& ipWhiteList() { return ip_white_list_; }
  GlobalStats& stats() { return stats_; }

private:
  ClientSslAuthConfig(
      const envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& config,
      ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
      Stats::Scope& scope, Runtime::RandomGenerator& random);

  static GlobalStats generateStats(Stats::Scope& scope, const std::string& prefix);

  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override {}
  void onFetchFailure(const EnvoyException* e) override;

  ThreadLocal::SlotPtr tls_;
  Network::Address::IpList ip_white_list_;
  GlobalStats stats_;
};

/**
 * A client SSL auth filter instance. One per connection.
 */
class ClientSslAuthFilter : public Network::ReadFilter, public Network::ConnectionCallbacks {
public:
  ClientSslAuthFilter(ClientSslAuthConfigSharedPtr config) : config_(config) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().addConnectionCallbacks(*this);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  ClientSslAuthConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // ClientSsl
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
