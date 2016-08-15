#pragma once

#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/json/json_loader.h"
#include "common/network/utility.h"

namespace Filter {
namespace Auth {
namespace ClientSsl {

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

  // ThreadLocal::ThreadLocalObject
  void shutdown() override {}

private:
  std::unordered_set<std::string> allowed_sha256_digests_;
};

typedef std::shared_ptr<AllowedPrincipals> AllowedPrincipalsPtr;

/**
 * Global configuration for client SSL authentication. The config contacts a JSON API to fetch the
 * list of allowed principals, caches it, then makes auth decisions on it and any associated IP
 * white list.
 */
class Config : public Http::AsyncClient::Callbacks {
public:
  Config(const Json::Object& config, ThreadLocal::Instance& tls, Upstream::ClusterManager& cm,
         Event::Dispatcher& dispatcher, Stats::Store& stats_store, Runtime::Loader& runtime,
         const std::string& local_address);

  const AllowedPrincipals& allowedPrincipals();
  const Network::IpWhiteList& ipWhiteList() { return ip_white_list_; }
  GlobalStats& stats() { return stats_; }

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& response) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

private:
  static GlobalStats generateStats(Stats::Store& store, const std::string& prefix);
  AllowedPrincipalsPtr parseAuthResponse(Http::Message& message);
  void refreshPrincipals();
  void requestComplete();

  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  Upstream::ClusterManager& cm_;
  const std::string auth_api_cluster_;
  Event::TimerPtr interval_timer_;
  Network::IpWhiteList ip_white_list_;
  GlobalStats stats_;
  Runtime::Loader& runtime_;
  const std::string local_address_;
};

typedef std::shared_ptr<Config> ConfigPtr;

/**
 * A client SSL auth filter instance. One per connection.
 */
class Instance : public Network::ReadFilter {
public:
  Instance(ConfigPtr config) : config_(config) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  ConfigPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  bool auth_checked_{};
};

} // ClientSsl
} // Auth
} // Filter
