#include "common/config/config_provider_impl.h"

namespace Envoy {
namespace Config {

StaticConfigProviderImpl::StaticConfigProviderImpl(
    Server::Configuration::FactoryContext& factory_context,
    ConfigProviderManagerImpl& config_provider_manager)
    : last_updated_(factory_context.timeSource().systemTime()),
      config_provider_manager_(config_provider_manager) {
  config_provider_manager_.static_config_providers_.insert(this);
}

StaticConfigProviderImpl::~StaticConfigProviderImpl() {
  config_provider_manager_.static_config_providers_.erase(this);
}

ConfigSubscriptionInstance::~ConfigSubscriptionInstance() {
  runInitializeCallbackIfAny();
  config_provider_manager_.unbindSubscription(manager_identifier_);
}

void ConfigSubscriptionInstance::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

bool ConfigSubscriptionInstance::checkAndApplyConfig(const Protobuf::Message& config_proto,
                                                     const std::string& config_name,
                                                     const std::string& version_info) {
  const uint64_t new_hash = MessageUtil::hash(config_proto);
  if (config_info_ && config_info_.value().last_config_hash_ == new_hash) {
    return false;
  }

  config_info_ = {new_hash, version_info};
  ENVOY_LOG(debug, "{}: loading new configuration: config_name={} hash={}", name_, config_name,
            new_hash);

  ASSERT(!dynamic_config_providers_.empty());
  ConfigProvider::ConfigConstSharedPtr new_config;
  for (auto* provider : dynamic_config_providers_) {
    if (new_config == nullptr) {
      new_config = provider->onConfigProtoUpdate(config_proto);
    }
    provider->onConfigUpdate(new_config);
  }

  return true;
}

ConfigProviderManagerImpl::ConfigProviderManagerImpl(Server::Admin& admin,
                                                     const std::string& config_name) {
  config_tracker_entry_ =
      admin.getConfigTracker().add(config_name, [this] { return dumpConfigs(); });
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

} // namespace Config
} // namespace Envoy
