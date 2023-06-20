#include "source/common/config/config_provider_impl.h"

namespace Envoy {
namespace Config {

ImmutableConfigProviderBase::ImmutableConfigProviderBase(
    Server::Configuration::ServerFactoryContext& factory_context,
    ConfigProviderManagerImplBase& config_provider_manager,
    ConfigProviderInstanceType instance_type, ApiType api_type)
    : last_updated_(factory_context.timeSource().systemTime()),
      config_provider_manager_(config_provider_manager), instance_type_(instance_type),
      api_type_(api_type) {
  ASSERT(instance_type_ == ConfigProviderInstanceType::Static ||
         instance_type_ == ConfigProviderInstanceType::Inline);
  config_provider_manager_.bindImmutableConfigProvider(this);
}

ImmutableConfigProviderBase::~ImmutableConfigProviderBase() {
  config_provider_manager_.unbindImmutableConfigProvider(this);
}

ConfigSubscriptionCommonBase::~ConfigSubscriptionCommonBase() {
  local_init_target_.ready();
  config_provider_manager_.unbindSubscription(manager_identifier_);
}

void ConfigSubscriptionCommonBase::applyConfigUpdate(const ConfigUpdateCb& update_fn) {
  tls_.runOnAllThreads([update_fn](OptRef<ThreadLocalConfig> thread_local_config) {
    thread_local_config->config_ = update_fn(thread_local_config->config_);
  });
}

bool ConfigSubscriptionInstance::checkAndApplyConfigUpdate(const Protobuf::Message& config_proto,
                                                           const std::string& config_name,
                                                           const std::string& version_info) {
  const uint64_t new_hash = MessageUtil::hash(config_proto);
  if (config_info_) {
    ASSERT(config_info_.value().last_config_hash_.has_value());
    if (config_info_.value().last_config_hash_.value() == new_hash) {
      return false;
    }
  }

  config_info_ = {new_hash, version_info};
  ENVOY_LOG(debug, "{}: loading new configuration: config_name={} hash={}", name_, config_name,
            new_hash);
  ConfigProvider::ConfigConstSharedPtr new_config_impl = onConfigProtoUpdate(config_proto);
  applyConfigUpdate([new_config_impl](ConfigProvider::ConfigConstSharedPtr)
                        -> ConfigProvider::ConfigConstSharedPtr { return new_config_impl; });
  return true;
}

ConfigProviderManagerImplBase::ConfigProviderManagerImplBase(OptRef<Server::Admin> admin,
                                                             const std::string& config_name) {
  if (!admin.has_value()) {
    return;
  }
  config_tracker_entry_ = admin->getConfigTracker().add(
      config_name,
      [this](const Matchers::StringMatcher& name_matcher) { return dumpConfigs(name_matcher); });
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

const ConfigProviderManagerImplBase::ConfigProviderSet&
ConfigProviderManagerImplBase::immutableConfigProviders(ConfigProviderInstanceType type) const {
  static ConfigProviderSet empty_set;
  ConfigProviderMap::const_iterator it;
  if ((it = immutable_config_providers_map_.find(type)) == immutable_config_providers_map_.end()) {
    return empty_set;
  }

  return *it->second;
}

void ConfigProviderManagerImplBase::bindImmutableConfigProvider(
    ImmutableConfigProviderBase* provider) {
  ConfigProviderMap::iterator it;
  if ((it = immutable_config_providers_map_.find(provider->instanceType())) ==
      immutable_config_providers_map_.end()) {
    immutable_config_providers_map_.insert(std::make_pair(
        provider->instanceType(),
        std::make_unique<ConfigProviderSet>(std::initializer_list<ConfigProvider*>({provider}))));
  } else {
    it->second->insert(provider);
  }
}

void ConfigProviderManagerImplBase::unbindImmutableConfigProvider(
    ImmutableConfigProviderBase* provider) {
  auto it = immutable_config_providers_map_.find(provider->instanceType());
  ASSERT(it != immutable_config_providers_map_.end());
  it->second->erase(provider);
}

} // namespace Config
} // namespace Envoy
