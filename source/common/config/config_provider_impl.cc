#include "common/config/config_provider_impl.h"

namespace Envoy {
namespace Config {

StaticConfigProviderImplBase::StaticConfigProviderImplBase(
    Server::Configuration::FactoryContext& factory_context,
    ConfigProviderManagerImplBase& config_provider_manager)
    : last_updated_(factory_context.timeSource().systemTime()),
      config_provider_manager_(config_provider_manager) {
  config_provider_manager_.static_config_providers_.insert(this);
}

StaticConfigProviderImplBase::~StaticConfigProviderImplBase() {
  config_provider_manager_.static_config_providers_.erase(this);
}

ConfigSubscriptionInstanceBase::~ConfigSubscriptionInstanceBase() {
  runInitializeCallbackIfAny();
  config_provider_manager_.unbindSubscription(manager_identifier_);
}

void ConfigSubscriptionInstanceBase::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

bool ConfigSubscriptionInstanceBase::checkAndApplyConfig(const Protobuf::Message& config_proto,
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
    // All bound dynamic config providers must be of the same type (see the ASSERT... in
    // bindConfigProvider()).
    // This makes it safe to call any of the provider's onConfigProtoUpdate() to get a new config
    // impl, which can then be passed to all providers.
    if (new_config == nullptr) {
      new_config = provider->onConfigProtoUpdate(config_proto);
    }
    provider->onConfigUpdate(new_config);
  }

  return true;
}

void ConfigSubscriptionInstanceBase::bindConfigProvider(DynamicConfigProviderImplBase* provider) {
  // All config providers bound to a ConfigSubscriptionInstanceBase must be of the same concrete
  // type; this is assumed by checkAndApplyConfig() and is verified by the assertion below. NOTE: a
  // regular ASSERT() triggers a potentially evaluated expression warning from clang due to the args
  // passed to the second typeid() call.
  ASSERT_IGNORE_POTENTIALLY_EVALUATED(dynamic_config_providers_.empty() ||
                                      typeid(*provider) ==
                                          typeid(**dynamic_config_providers_.begin()));
  dynamic_config_providers_.insert(provider);
}

ConfigProviderManagerImplBase::ConfigProviderManagerImplBase(Server::Admin& admin,
                                                             const std::string& config_name) {
  config_tracker_entry_ =
      admin.getConfigTracker().add(config_name, [this] { return dumpConfigs(); });
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

} // namespace Config
} // namespace Envoy
