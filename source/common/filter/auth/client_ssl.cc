#include "common/filter/auth/client_ssl.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Filter {
namespace Auth {
namespace ClientSsl {

Config::Config(const envoy::api::v2::filter::network::ClientSSLAuth& config,
               ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm,
               Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::RandomGenerator& random)
    : RestApiFetcher(
          cm, config.auth_api_cluster(), dispatcher, random,
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, refresh_delay, 60000))),
      tls_(tls.allocateSlot()), ip_white_list_(config.ip_white_list()),
      stats_(generateStats(scope, config.stat_prefix())) {

  if (!cm.get(remote_cluster_name_)) {
    throw EnvoyException(
        fmt::format("unknown cluster '{}' in client ssl auth config", remote_cluster_name_));
  }

  AllowedPrincipalsSharedPtr empty(new AllowedPrincipals());
  tls_->set(
      [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return empty; });
}

ConfigSharedPtr Config::create(const envoy::api::v2::filter::network::ClientSSLAuth& config,
                               ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm,
                               Event::Dispatcher& dispatcher, Stats::Scope& scope,
                               Runtime::RandomGenerator& random) {
  ConfigSharedPtr new_config(new Config(config, tls, cm, dispatcher, scope, random));
  new_config->initialize();
  return new_config;
}

const AllowedPrincipals& Config::allowedPrincipals() { return tls_->getTyped<AllowedPrincipals>(); }

GlobalStats Config::generateStats(Stats::Scope& scope, const std::string& prefix) {
  std::string final_prefix = fmt::format("auth.clientssl.{}.", prefix);
  GlobalStats stats{ALL_CLIENT_SSL_AUTH_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                              POOL_GAUGE_PREFIX(scope, final_prefix))};
  return stats;
}

void Config::parseResponse(const Http::Message& message) {
  AllowedPrincipalsSharedPtr new_principals(new AllowedPrincipals());
  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(message.bodyAsString());
  for (const Json::ObjectSharedPtr& certificate : loader->getObjectArray("certificates")) {
    new_principals->add(certificate->getString("fingerprint_sha256"));
  }

  tls_->set([new_principals](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return new_principals;
  });

  stats_.update_success_.inc();
  stats_.total_principals_.set(new_principals->size());
}

void Config::onFetchFailure(const EnvoyException*) { stats_.update_failure_.inc(); }

static const std::string Path = "/v1/certs/list/approved";

void Config::createRequest(Http::Message& request) {
  request.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
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

void Instance::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected) {
    return;
  }

  ASSERT(read_callbacks_->connection().ssl());
  if (config_->ipWhiteList().contains(*read_callbacks_->connection().remoteAddress())) {
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

} // namespace ClientSsl
} // namespace Auth
} // namespace Filter
} // namespace Envoy
