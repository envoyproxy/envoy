#include "common/secret/sds_api.h"

#include <unordered_map>

#include "envoy/api/v2/auth/cert.pb.validate.h"

#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/secret/secret_manager_util.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

SdsApi::SdsApi(Server::Instance& server, const envoy::api::v2::core::ConfigSource& sds_config,
               std::string sds_config_name)
    : server_(server), sds_config_(sds_config), sds_config_name_(sds_config_name) {
  server_.initManager().registerTarget(*this);
}

void SdsApi::initialize(std::function<void()> callback) {
  initialize_callback_ = callback;
  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::auth::Secret>(
      sds_config_, server_.localInfo().node(), server_.dispatcher(), server_.clusterManager(),
      server_.random(), server_.stats(), /* rest_legacy_constructor */ nullptr,
      "envoy.service.discovery.v2.SecretDiscoveryService.FetchSecrets",
      // TODO(jaebong): replace next line with
      // "envoy.service.discovery.v2.SecretDiscoveryService.StreamSecrets" to support streaming
      // service
      "envoy.service.discovery.v2.SecretDiscoveryService.FetchSecrets");

  Config::Utility::checkLocalInfo("sds", server_.localInfo());

  subscription_->start({sds_config_name_}, *this);
}

void SdsApi::onConfigUpdate(const ResourceVector& resources, const std::string&) {
  if (resources.empty()) {
    runInitializeCallbackIfAny();
    return;
  }
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected RDS resource length: {}", resources.size()));
  }
  const auto& secret = resources[0];
  MessageUtil::validate(secret);
  if (!(secret.name() == sds_config_name_)) {
    throw EnvoyException(fmt::format("Unexpected RDS configuration (expecting {}): {}",
                                     sds_config_name_, secret.name()));
  }

  const uint64_t new_hash = MessageUtil::hash(secret);
  if (new_hash != secret_hash_) {
    if (secret.type_case() == envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate) {
      tls_certificate_secrets_ =
          std::make_shared<Ssl::TlsCertificateConfigImpl>(secret.tls_certificate());
      secret_hash_ = new_hash;

      for (auto cb : update_callbacks_) {
        cb->onAddOrUpdateSecret();
      }
    }
  }

  runInitializeCallbackIfAny();
}

void SdsApi::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad config.
  runInitializeCallbackIfAny();
}

void SdsApi::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // namespace Secret
} // namespace Envoy
