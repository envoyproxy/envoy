#include "contrib/client_ssl_auth/filters/network/source/client_ssl_auth.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/utility.h"

#include "contrib/envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

constexpr absl::string_view AuthDigestNoMatch = "auth_digest_no_match";

ClientSslAuthConfig::ClientSslAuthConfig(
    const envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth& config,
    ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
    Stats::Scope& scope, Random::RandomGenerator& random)
    : RestApiFetcher(
          cm, config.auth_api_cluster(), dispatcher, random,
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, refresh_delay, 60000)),
          std::chrono::milliseconds(1000)),
      tls_(tls.allocateSlot()), stats_(generateStats(scope, config.stat_prefix())) {
  auto list_or_error = Network::Address::IpList::create(config.ip_white_list());
  THROW_IF_NOT_OK_REF(list_or_error.status());
  ip_allowlist_ = std::move(list_or_error.value());

  if (!cm.clusters().hasCluster(remote_cluster_name_)) {
    throw EnvoyException(
        fmt::format("unknown cluster '{}' in client ssl auth config", remote_cluster_name_));
  }

  AllowedPrincipalsSharedPtr empty(new AllowedPrincipals());
  tls_->set(
      [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return empty; });
}

ClientSslAuthConfigSharedPtr ClientSslAuthConfig::create(
    const envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth& config,
    ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
    Stats::Scope& scope, Random::RandomGenerator& random) {
  ClientSslAuthConfigSharedPtr new_config(
      new ClientSslAuthConfig(config, tls, cm, dispatcher, scope, random));
  new_config->initialize();
  return new_config;
}

const AllowedPrincipals& ClientSslAuthConfig::allowedPrincipals() {
  return tls_->getTyped<AllowedPrincipals>();
}

GlobalStats ClientSslAuthConfig::generateStats(Stats::Scope& scope, const std::string& prefix) {
  std::string final_prefix = fmt::format("auth.clientssl.{}.", prefix);
  GlobalStats stats{ALL_CLIENT_SSL_AUTH_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                              POOL_GAUGE_PREFIX(scope, final_prefix))};
  return stats;
}

void ClientSslAuthConfig::parseResponse(const Http::ResponseMessage& message) {
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

void ClientSslAuthConfig::onFetchFailure(Config::ConfigUpdateFailureReason, const EnvoyException*) {
  stats_.update_failure_.inc();
}

static const std::string Path = "/v1/certs/list/approved";

void ClientSslAuthConfig::createRequest(Http::RequestMessage& request) {
  request.headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
  request.headers().setPath(Path);
}

Network::FilterStatus ClientSslAuthFilter::onData(Buffer::Instance&, bool) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ClientSslAuthFilter::onNewConnection() {
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

void ClientSslAuthFilter::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected) {
    return;
  }

  ASSERT(read_callbacks_->connection().ssl());
  if (config_->ipAllowlist().contains(
          *read_callbacks_->connection().connectionInfoProvider().remoteAddress())) {
    config_->stats().auth_ip_allowlist_.inc();
    read_callbacks_->continueReading();
    return;
  }

  if (!config_->allowedPrincipals().allowed(
          read_callbacks_->connection().ssl()->sha256PeerCertificateDigest())) {
    read_callbacks_->connection().streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::UpstreamProtocolError);
    read_callbacks_->connection().streamInfo().setResponseCodeDetails(AuthDigestNoMatch);
    config_->stats().auth_digest_no_match_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                        "auth_digest_no_match");
    return;
  }

  config_->stats().auth_digest_match_.inc();
  read_callbacks_->continueReading();
}

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
