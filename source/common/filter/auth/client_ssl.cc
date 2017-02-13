#include "client_ssl.h"

#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/network/utility.h"

namespace Filter {
namespace Auth {
namespace ClientSsl {

Config::Config(const Json::Object& config, ThreadLocal::Instance& tls, Upstream::ClusterManager& cm,
               Event::Dispatcher& dispatcher, Stats::Store& stats_store,
               Runtime::RandomGenerator& random)
    : RestApiFetcher(cm, config.getString("auth_api_cluster"), dispatcher, random,
                     std::chrono::milliseconds(config.getInteger("refresh_interval_ms", 60000))),
      tls_(tls), tls_slot_(tls.allocateSlot()), ip_white_list_(config, "ip_white_list"),
      stats_(generateStats(stats_store, config.getString("stat_prefix"))) {

  config.validateSchema(Json::Schema::CLIENT_SSL_NETWORK_FILTER_SCHEMA);

  if (!cm.get(remote_cluster_name_)) {
    throw EnvoyException(
        fmt::format("unknown cluster '{}' in client ssl auth config", remote_cluster_name_));
  }

  AllowedPrincipalsPtr empty(new AllowedPrincipals());
  tls_.set(tls_slot_,
           [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectPtr { return empty; });
}

ConfigPtr Config::create(const Json::Object& config, ThreadLocal::Instance& tls,
                         Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                         Stats::Store& stats_store, Runtime::RandomGenerator& random) {
  ConfigPtr new_config(new Config(config, tls, cm, dispatcher, stats_store, random));
  new_config->initialize();
  return new_config;
}

const AllowedPrincipals& Config::allowedPrincipals() {
  return tls_.getTyped<AllowedPrincipals>(tls_slot_);
}

GlobalStats Config::generateStats(Stats::Store& store, const std::string& prefix) {
  std::string final_prefix = fmt::format("auth.clientssl.{}.", prefix);
  GlobalStats stats{ALL_CLIENT_SSL_AUTH_STATS(POOL_COUNTER_PREFIX(store, final_prefix),
                                              POOL_GAUGE_PREFIX(store, final_prefix))};
  return stats;
}

void Config::parseResponse(const Http::Message& message) {
  AllowedPrincipalsPtr new_principals(new AllowedPrincipals());
  Json::ObjectPtr loader = Json::Factory::LoadFromString(message.bodyAsString());
  for (const Json::ObjectPtr& certificate : loader->getObjectArray("certificates")) {
    new_principals->add(certificate->getString("fingerprint_sha256"));
  }

  tls_.set(tls_slot_, [new_principals](Event::Dispatcher&)
                          -> ThreadLocal::ThreadLocalObjectPtr { return new_principals; });

  stats_.update_success_.inc();
  stats_.total_principals_.set(new_principals->size());
}

void Config::onFetchFailure(EnvoyException*) { stats_.update_failure_.inc(); }

static const std::string Path = "/v1/certs/list/approved";

void Config::createRequest(Http::Message& request) {
  request.headers().insertMethod().value(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(Path);
}

Network::FilterStatus Instance::onData(Buffer::Instance&) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus Instance::onNewConnection() {
  // If this is not an SSL connection, do no further checking. High layers should redirect, etc.
  // if SSL is required.
  if (!read_callbacks_->connection().ssl()) {
    config_->stats().auth_no_ssl_.inc();
    return Network::FilterStatus::Continue;
  } else {
    // Otherwise we need to wait for handshake to be complete before proceeding.
    return Network::FilterStatus::StopIteration;
  }
}

void Instance::onEvent(uint32_t events) {
  if (!(events & Network::ConnectionEvent::Connected)) {
    return;
  }

  ASSERT(read_callbacks_->connection().ssl());
  if (config_->ipWhiteList().contains(read_callbacks_->connection().remoteAddress())) {
    config_->stats().auth_ip_white_list_.inc();
    read_callbacks_->continueReading();
    return;
  }

  if (!config_->allowedPrincipals().allowed(
          read_callbacks_->connection().ssl()->sha256PeerCertificateDigest())) {
    config_->stats().auth_digest_no_match_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }

  config_->stats().auth_digest_match_.inc();
  read_callbacks_->continueReading();
}

} // Client Ssl
} // Auth
} // Filter
