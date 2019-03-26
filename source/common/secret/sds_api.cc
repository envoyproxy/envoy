#include "common/secret/sds_api.h"

#include <unordered_map>

#include "envoy/api/v2/auth/cert.pb.validate.h"

#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Secret {

SdsApi::SdsApi(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
               Runtime::RandomGenerator& random, Stats::Store& stats,
               Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
               const envoy::api::v2::core::ConfigSource& sds_config,
               const std::string& sds_config_name, std::function<void()> destructor_cb,
               Api::Api& api)
    : init_target_(fmt::format("SdsApi {}", sds_config_name), [this] { initialize(); }),
      local_info_(local_info), dispatcher_(dispatcher), random_(random), stats_(stats),
      cluster_manager_(cluster_manager), sds_config_(sds_config), sds_config_name_(sds_config_name),
      secret_hash_(0), clean_up_(destructor_cb), api_(api) {
  Config::Utility::checkLocalInfo("sds", local_info_);
  // TODO(JimmyCYJ): Implement chained_init_manager, so that multiple init_manager
  // can be chained together to behave as one init_manager. In that way, we let
  // two listeners which share same SdsApi to register at separate init managers, and
  // each init manager has a chance to initialize its targets.
  init_manager.add(init_target_);
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

  // Wrap sds_config_name_ in string_view to deal with proto string/std::string incompatibility
  // issues within Google.
  if (secret.name() != absl::string_view(sds_config_name_)) {
    throw EnvoyException(
        fmt::format("Unexpected SDS secret (expecting {}): {}", sds_config_name_, secret.name()));
  }

  const uint64_t new_hash = MessageUtil::hash(secret);
  if (new_hash != secret_hash_) {
    validateConfig(secret);
    secret_hash_ = new_hash;
    setSecret(secret);
    update_callback_manager_.runCallbacks();
  }

  init_target_.ready();
}

void SdsApi::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad config.
  init_target_.ready();
}

void SdsApi::initialize() {
  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::auth::Secret>(
      sds_config_, local_info_, dispatcher_, cluster_manager_, random_, stats_,
      "envoy.service.discovery.v2.SecretDiscoveryService.FetchSecrets",
      "envoy.service.discovery.v2.SecretDiscoveryService.StreamSecrets", api_);
  subscription_->start({sds_config_name_}, *this);
}

} // namespace Secret
} // namespace Envoy
