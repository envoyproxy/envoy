#include "common/config/config_provider_impl.h"

namespace Envoy {
namespace Config {

ImmutableConfigProviderBase::ImmutableConfigProviderBase(
    Server::Configuration::FactoryContext& factory_context,
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
  init_target_.ready();
  config_provider_manager_.unbindSubscription(manager_identifier_);
}

void ConfigSubscriptionCommonBase::bindConfigProvider(MutableConfigProviderCommonBase* provider) {
  // All config providers bound to a ConfigSubscriptionCommonBase must be of the same concrete
  // type; this is assumed by ConfigSubscriptionInstance::checkAndApplyConfigUpdate() and is
  // verified by the assertion below. NOTE: an inlined statement ASSERT() triggers a potentially
  // evaluated expression warning from clang due to `typeid(**mutable_config_providers_.begin())`.
  // To avoid this, we use a lambda to separate the first mutable provider dereference from the
  // typeid() statement.
  ASSERT([&]() {
    if (!mutable_config_providers_.empty()) {
      const auto& first_provider = **mutable_config_providers_.begin();
      return typeid(*provider) == typeid(first_provider);
    }
    return true;
  }());
  mutable_config_providers_.insert(provider);
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

  ASSERT(!mutable_config_providers_.empty());
  ConfigProvider::ConfigConstSharedPtr new_config;
  for (auto* provider : mutable_config_providers_) {
    // All bound mutable config providers must be of the same type (see the ASSERT... in
    // bindConfigProvider()).
    // This makes it safe to call any of the provider's onConfigProtoUpdate() to get a new config
    // impl, which can then be passed to all providers.
    auto* typed_provider = static_cast<MutableConfigProviderBase*>(provider);
    if (new_config == nullptr) {
      if ((new_config = typed_provider->onConfigProtoUpdate(config_proto)) == nullptr) {
        return false;
      }
    }
    typed_provider->onConfigUpdate(new_config);
  }

  return true;
}

void DeltaConfigSubscriptionInstance::applyDeltaConfigUpdate(
    const std::function<void(const ConfigSharedPtr&)>& update_fn) {
  // The Config implementation is assumed to be shared across the config providers bound to this
  // subscription, therefore, simply propagating the update to all worker threads for a single bound
  // provider will be sufficient.
  if (mutable_config_providers_.size() > 1) {
    ASSERT(static_cast<DeltaMutableConfigProviderBase*>(*mutable_config_providers_.begin())
               ->getConfig() == static_cast<DeltaMutableConfigProviderBase*>(
                                    *std::next(mutable_config_providers_.begin()))
                                    ->getConfig());
  }

  // TODO(AndresGuedez): currently, the caller has to compute the differences in resources between
  // DS API config updates and passes a granular update_fn() that adds/modifies/removes resources as
  // needed. Such logic could be generalized as part of this framework such that this function owns
  // the diffing and issues the corresponding call to add/modify/remove a resource according to a
  // vector of functions passed by the caller.
  auto* typed_provider =
      static_cast<DeltaMutableConfigProviderBase*>(getAnyBoundMutableConfigProvider());
  ConfigSharedPtr config = typed_provider->getConfig();
  typed_provider->onConfigUpdate([config, update_fn]() { update_fn(config); });
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
