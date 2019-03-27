#include "common/config/config_provider_impl.h"

namespace Envoy {
namespace Config {

ImmutableConfigProviderImplBase::ImmutableConfigProviderImplBase(
    Server::Configuration::FactoryContext& factory_context,
    ConfigProviderManagerImplBase& config_provider_manager, ConfigProviderInstanceType type)
    : last_updated_(factory_context.timeSource().systemTime()),
      config_provider_manager_(config_provider_manager), type_(type) {
  config_provider_manager_.bindImmutableConfigProvider(this);
}

ImmutableConfigProviderImplBase::~ImmutableConfigProviderImplBase() {
  config_provider_manager_.unbindImmutableConfigProvider(this);
}

ConfigSubscriptionInstanceBase::~ConfigSubscriptionInstanceBase() {
  init_target_.ready();
  config_provider_manager_.unbindSubscription(manager_identifier_);
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

  ASSERT(!mutable_config_providers_.empty());
  ConfigProvider::ConfigConstSharedPtr new_config;
  for (auto* provider : mutable_config_providers_) {
    // All bound mutable config providers must be of the same type (see the ASSERT... in
    // bindConfigProvider()).
    // This makes it safe to call any of the provider's onConfigProtoUpdate() to get a new config
    // impl, which can then be passed to all providers.
    if (new_config == nullptr) {
      if ((new_config = provider->onConfigProtoUpdate(config_proto)) == nullptr) {
        return false;
      }
    }
    provider->onConfigUpdate(new_config);
  }

  return true;
}

void ConfigSubscriptionInstanceBase::bindConfigProvider(MutableConfigProviderImplBase* provider) {
  // All config providers bound to a ConfigSubscriptionInstanceBase must be of the same concrete
  // type; this is assumed by checkAndApplyConfig() and is verified by the assertion below.
  // NOTE: an inlined statement ASSERT() triggers a potentially evaluated expression warning from
  // clang due to `typeid(**mutable_config_providers_.begin())`. To avoid this, we use a lambda to
  // separate the first mutable provider dereference from the typeid() statement.
  ASSERT([&]() {
    if (!mutable_config_providers_.empty()) {
      const auto& first_provider = **mutable_config_providers_.begin();
      return typeid(*provider) == typeid(first_provider);
    }
    return true;
  }());
  mutable_config_providers_.insert(provider);
}

ConfigProviderManagerImplBase::ConfigProviderManagerImplBase(Server::Admin& admin,
                                                             const std::string& config_name) {
  config_tracker_entry_ =
      admin.getConfigTracker().add(config_name, [this] { return dumpConfigs(); });
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
    ImmutableConfigProviderImplBase* provider) {
  ASSERT(provider->type() == ConfigProviderInstanceType::Static ||
         provider->type() == ConfigProviderInstanceType::Inline);
  ConfigProviderMap::iterator it;
  if ((it = immutable_config_providers_map_.find(provider->type())) ==
      immutable_config_providers_map_.end()) {
    immutable_config_providers_map_.insert(std::make_pair(
        provider->type(),
        std::make_unique<ConfigProviderSet>(std::initializer_list<ConfigProvider*>({provider}))));
  } else {
    it->second->insert(provider);
  }
}

void ConfigProviderManagerImplBase::unbindImmutableConfigProvider(
    ImmutableConfigProviderImplBase* provider) {
  ASSERT(provider->type() == ConfigProviderInstanceType::Static ||
         provider->type() == ConfigProviderInstanceType::Inline);
  auto it = immutable_config_providers_map_.find(provider->type());
  ASSERT(it != immutable_config_providers_map_.end());
  it->second->erase(provider);
}

} // namespace Config
} // namespace Envoy
