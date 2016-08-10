#include "client_ssl.h"

#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

namespace Filter {
namespace Auth {
namespace ClientSsl {

Config::Config(const Json::Object& config, ThreadLocal::Instance& tls, Upstream::ClusterManager& cm,
               Event::Dispatcher& dispatcher, Stats::Store& stats_store, Runtime::Loader& runtime,
               const std::string& local_address)
    : tls_(tls), tls_slot_(tls.allocateSlot()), cm_(cm),
      auth_api_cluster_(config.getString("auth_api_cluster")),
      interval_timer_(dispatcher.createTimer([this]() -> void { refreshPrincipals(); })),
      ip_white_list_(config), stats_(generateStats(stats_store, config.getString("stat_prefix"))),
      runtime_(runtime), local_address_(local_address) {

  if (!cm_.has(auth_api_cluster_)) {
    throw EnvoyException(
        fmt::format("unknown cluster '{}' in client ssl auth config", auth_api_cluster_));
  }

  AllowedPrincipalsPtr empty(new AllowedPrincipals());
  tls_.set(tls_slot_,
           [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectPtr { return empty; });

  refreshPrincipals();
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

AllowedPrincipalsPtr Config::parseAuthResponse(Http::Message& message) {
  AllowedPrincipalsPtr new_principals(new AllowedPrincipals());
  Json::StringLoader loader(message.bodyAsString());
  for (const Json::Object& certificate : loader.getObjectArray("certificates")) {
    new_principals->add(certificate.getString("fingerprint_sha256"));
  }

  return new_principals;
}

void Config::onSuccess(Http::MessagePtr&& response) {
  uint64_t response_code = Http::Utility::getResponseStatus(response->headers());
  if (response_code != enumToInt(Http::Code::OK)) {
    onFailure(Http::AsyncClient::FailureReason::Reset);
    return;
  }

  AllowedPrincipalsPtr new_principals;
  try {
    new_principals = parseAuthResponse(*response);
  } catch (EnvoyException& e) {
    onFailure(Http::AsyncClient::FailureReason::Reset);
    return;
  }

  tls_.set(tls_slot_, [new_principals](Event::Dispatcher&)
                          -> ThreadLocal::ThreadLocalObjectPtr { return new_principals; });

  stats_.update_success_.inc();
  stats_.total_principals_.set(new_principals->size());
  requestComplete();
}

void Config::onFailure(Http::AsyncClient::FailureReason) {
  stats_.update_failure_.inc();
  requestComplete();
}

void Config::refreshPrincipals() {
  ASSERT(!active_request_);
  active_request_.reset(new ActiveRequest());
  active_request_->client_ = cm_.httpAsyncClientForCluster(auth_api_cluster_);
  if (!active_request_->client_) {
    onFailure(Http::AsyncClient::FailureReason::Reset);
    return;
  }

  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().addViaMoveValue(Http::Headers::get().Scheme, "http");
  message->headers().addViaMoveValue(Http::Headers::get().Method, "GET");
  message->headers().addViaMoveValue(Http::Headers::get().Path, "/v1/certs/list/approved");
  message->headers().addViaCopy(Http::Headers::get().Host, auth_api_cluster_);
  message->headers().addViaCopy(Http::Headers::get().ForwardedFor, local_address_);
  active_request_->request_ = active_request_->client_->send(std::move(message), *this,
                                                             Optional<std::chrono::milliseconds>());
}

void Config::requestComplete() {
  std::chrono::milliseconds interval(
      runtime_.snapshot().getInteger("auth.clientssl.refresh_interval_ms", 60000));

  active_request_.reset();
  interval_timer_->enableTimer(interval);
}

Network::FilterStatus Instance::onData(Buffer::Instance&) {
  if (auth_checked_) {
    return Network::FilterStatus::Continue;
  }

  auth_checked_ = true;

  // If this is not an SSL connection, do no further checking. High layers should redirect, etc.
  // if SSL is required.
  if (!read_callbacks_->connection().ssl()) {
    config_->stats().auth_no_ssl_.inc();
    return Network::FilterStatus::Continue;
  }

  if (config_->ipWhiteList().contains(read_callbacks_->connection().remoteAddress())) {
    config_->stats().auth_ip_white_list_.inc();
    return Network::FilterStatus::Continue;
  }

  if (!config_->allowedPrincipals().allowed(
          read_callbacks_->connection().ssl()->sha256PeerCertificateDigest())) {
    config_->stats().auth_digest_no_match_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  config_->stats().auth_digest_match_.inc();
  return Network::FilterStatus::Continue;
}

} // Client Ssl
} // Auth
} // Filter
