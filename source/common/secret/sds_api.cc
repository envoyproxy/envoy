#include "common/secret/sds_api.h"

#include <unordered_map>

#include "envoy/api/v2/auth/cert.pb.validate.h"

#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/protobuf/utility.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

SdsApi::SdsApi(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
               Runtime::RandomGenerator& random, Stats::Store& stats,
               Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
               const envoy::api::v2::core::ConfigSource& sds_config, std::string sds_config_name)
    : local_info_(local_info), dispatcher_(dispatcher), random_(random), stats_(stats),
      cluster_manager_(cluster_manager), sds_config_(sds_config), sds_config_name_(sds_config_name),
      secret_hash_(0) {
  init_manager.registerTarget(*this);
}

void SdsApi::initialize(std::function<void()> callback) {
  initialize_callback_ = callback;

  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::auth::Secret>(
      sds_config_, local_info_.node(), dispatcher_, cluster_manager_, random_, stats_,
      /* rest_legacy_constructor */ nullptr,
      "envoy.service.discovery.v2.SecretDiscoveryService.FetchSecrets",
      "envoy.service.discovery.v2.SecretDiscoveryService.StreamSecrets");
  Config::Utility::checkLocalInfo("sds", local_info_);

  subscription_->start({sds_config_name_}, *this);
}

void SdsApi::onConfigUpdate(const ResourceVector& resources, const std::string&) {
  if (resources.empty()) {
    throw EnvoyException(
        fmt::format("Missing SDS resources for {} in onConfigUpdate()", sds_config_name_));
  }
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected SDS secrets length: {}", resources.size()));
  }
  const auto& secret = resources[0];
  MessageUtil::validate(secret);
  if (!(secret.name() == sds_config_name_)) {
    throw EnvoyException(
        fmt::format("Unexpected SDS secret (expecting {}): {}", sds_config_name_, secret.name()));
  }

  ENVOY_LOG(info, "Received secret (name: {}), content: {}", secret.DebugString(), secret.name());

  const uint64_t new_hash = MessageUtil::hash(secret);
  if (new_hash != secret_hash_ &&
      secret.type_case() == envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate) {
    secret_hash_ = new_hash;
    tls_certificate_secrets_ =
        std::make_unique<Ssl::TlsCertificateConfigImpl>(secret.tls_certificate());

    for (auto cb : update_callbacks_) {
      cb->onAddOrUpdateSecret();
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
