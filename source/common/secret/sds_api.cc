#include "common/secret/sds_api.h"

#include <unordered_map>

#include "envoy/api/v2/auth/cert.pb.validate.h"

#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/protobuf/utility.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

template class Secret::SdsApi<Ssl::TlsCertificateConfig>;
template class Secret::SdsApi<Ssl::CertificateValidationContextConfig>;

template <class SecretType>
SdsApi<SecretType>::SdsApi(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                           Runtime::RandomGenerator& random, Stats::Store& stats,
                           Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                           const envoy::api::v2::core::ConfigSource& sds_config,
                           const std::string& sds_config_name, std::function<void()> destructor_cb)
    : secret_hash_(0), local_info_(local_info), dispatcher_(dispatcher), random_(random),
      stats_(stats), cluster_manager_(cluster_manager), sds_config_(sds_config),
      sds_config_name_(sds_config_name), clean_up_(destructor_cb) {
  // TODO(JimmyCYJ): Implement chained_init_manager, so that multiple init_manager
  // can be chained together to behave as one init_manager. In that way, we let
  // two listeners which share same SdsApi to register at separate init managers, and
  // each init manager has a chance to initialize its targets.
  init_manager.registerTarget(*this);
}

template <class SecretType> void SdsApi<SecretType>::initialize(std::function<void()> callback) {
  initialize_callback_ = callback;

  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::auth::Secret>(
      sds_config_, local_info_, dispatcher_, cluster_manager_, random_, stats_,
      /* rest_legacy_constructor */ nullptr,
      "envoy.service.discovery.v2.SecretDiscoveryService.FetchSecrets",
      "envoy.service.discovery.v2.SecretDiscoveryService.StreamSecrets");
  Config::Utility::checkLocalInfo("sds", local_info_);

  subscription_->start({sds_config_name_}, *this);
}

template <class SecretType>
void SdsApi<SecretType>::onConfigUpdate(const ResourceVector& resources, const std::string&) {
  if (resources.empty()) {
    throw EnvoyException(
        fmt::format("Missing SDS resources for {} in onConfigUpdate()", sds_config_name_));
  }
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected SDS secrets length: {}", resources.size()));
  }
  const auto& secret = resources[0];
  MessageUtil::validate(secret);

  // Wrap sds_config_name_ in string_view to deal with proto string/std::string incompatibility
  // issues within Google.
  if (secret.name() != absl::string_view(sds_config_name_)) {
    throw EnvoyException(
        fmt::format("Unexpected SDS secret (expecting {}): {}", sds_config_name_, secret.name()));
  }

  updateConfigHelper(secret);

  runInitializeCallbackIfAny();
}

template <class SecretType> void SdsApi<SecretType>::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad config.
  runInitializeCallbackIfAny();
}

template <class SecretType> void SdsApi<SecretType>::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

void TlsCertificateSdsApi::updateConfigHelper(const envoy::api::v2::auth::Secret& secret) {
  const uint64_t new_hash = MessageUtil::hash(secret);
  if (new_hash != secret_hash_ &&
      secret.type_case() == envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate) {
    secret_hash_ = new_hash;
    secrets_ = std::make_unique<Ssl::TlsCertificateConfigImpl>(secret.tls_certificate());

    update_callback_manager_.runCallbacks();
  }
}

void CertificateValidationContextSdsApi::updateConfigHelper(
    const envoy::api::v2::auth::Secret& secret) {
  const uint64_t new_hash = MessageUtil::hash(secret);
  if (new_hash != secret_hash_ &&
      secret.type_case() == envoy::api::v2::auth::Secret::TypeCase::kValidationContext) {
    secret_hash_ = new_hash;
    secrets_ =
        std::make_unique<Ssl::CertificateValidationContextConfigImpl>(secret.validation_context());

    update_callback_manager_.runCallbacks();
  }
}

} // namespace Secret
} // namespace Envoy
