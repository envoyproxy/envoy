#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/config/subscription.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/rest_api_fetcher.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/node_hash_set.h"
#include "contrib/envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

/**
 * All client SSL auth stats. @see stats_macros.h
 */
#define ALL_CLIENT_SSL_AUTH_STATS(COUNTER, GAUGE)                                                  \
  COUNTER(auth_digest_match)                                                                       \
  COUNTER(auth_digest_no_match)                                                                    \
  COUNTER(auth_ip_allowlist)                                                                       \
  COUNTER(auth_no_ssl)                                                                             \
  COUNTER(update_failure)                                                                          \
  COUNTER(update_success)                                                                          \
  GAUGE(total_principals, NeverImport)

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
  absl::node_hash_set<std::string> allowed_sha256_digests_;
};

using AllowedPrincipalsSharedPtr = std::shared_ptr<AllowedPrincipals>;

class ClientSslAuthConfig;
using ClientSslAuthConfigSharedPtr = std::shared_ptr<ClientSslAuthConfig>;

/**
 * Global configuration for client SSL authentication. The config contacts a JSON API to fetch the
 * list of allowed principals, caches it, then makes auth decisions on it and any associated IP
 * allowlist.
 */
class ClientSslAuthConfig : public Http::RestApiFetcher {
public:
  static ClientSslAuthConfigSharedPtr
  create(const envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth& config,
         ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm,
         Event::Dispatcher& dispatcher, Stats::Scope& scope, Random::RandomGenerator& random);

  const AllowedPrincipals& allowedPrincipals();
  const Network::Address::IpList& ipAllowlist() { return ip_allowlist_; }
  GlobalStats& stats() { return stats_; }

private:
  ClientSslAuthConfig(
      const envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth& config,
      ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
      Stats::Scope& scope, Random::RandomGenerator& random);

  static GlobalStats generateStats(Stats::Scope& scope, const std::string& prefix);

  // Http::RestApiFetcher
  void createRequest(Http::RequestMessage& request) override;
  void parseResponse(const Http::ResponseMessage& response) override;
  void onFetchComplete() override {}
  void onFetchFailure(Config::ConfigUpdateFailureReason reason, const EnvoyException* e) override;

  ThreadLocal::SlotPtr tls_;
  Network::Address::IpList ip_allowlist_;
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

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
