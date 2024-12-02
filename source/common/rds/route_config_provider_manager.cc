#include "source/common/rds/route_config_provider_manager.h"

#include "source/common/rds/util.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Rds {

RouteConfigProviderManager::RouteConfigProviderManager(OptRef<Server::Admin> admin,
                                                       const std::string& config_tracker_key,
                                                       ProtoTraits& proto_traits)
    : proto_traits_(proto_traits) {
  if (!admin.has_value()) {
    return;
  }
  config_tracker_entry_ = admin->getConfigTracker().add(
      config_tracker_key,
      [this](const Matchers::StringMatcher& matcher) { return dumpRouteConfigs(matcher); });
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the "routes" key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

void RouteConfigProviderManager::eraseStaticProvider(RouteConfigProvider* provider) {
  static_route_config_providers_.erase(provider);
}

void RouteConfigProviderManager::eraseDynamicProvider(uint64_t manager_identifier) {
  dynamic_route_config_providers_.erase(manager_identifier);
}

std::unique_ptr<envoy::admin::v3::RoutesConfigDump>
RouteConfigProviderManager::dumpRouteConfigs(const Matchers::StringMatcher& name_matcher) const {
  auto config_dump = std::make_unique<envoy::admin::v3::RoutesConfigDump>();

  for (const auto& element : dynamic_route_config_providers_) {
    const auto provider = element.second.first.lock();
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    ASSERT(provider);

    if (provider->configInfo()) {
      if (!name_matcher.match(
              resourceName(proto_traits_, provider->configInfo().value().config_))) {
        continue;
      }
      auto* dynamic_config = config_dump->mutable_dynamic_route_configs()->Add();
      dynamic_config->set_version_info(provider->configInfo().value().version_);
      MessageUtil::packFrom(*dynamic_config->mutable_route_config(),
                            provider->configInfo().value().config_);
      TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                            *dynamic_config->mutable_last_updated());
    }
  }

  for (const auto& provider : static_route_config_providers_) {
    ASSERT(provider->configInfo());
    if (!name_matcher.match(resourceName(proto_traits_, provider->configInfo().value().config_))) {
      continue;
    }
    auto* static_config = config_dump->mutable_static_route_configs()->Add();
    MessageUtil::packFrom(*static_config->mutable_route_config(),
                          provider->configInfo().value().config_);
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *static_config->mutable_last_updated());
  }

  return config_dump;
}

RouteConfigProviderPtr RouteConfigProviderManager::addStaticProvider(
    std::function<RouteConfigProviderPtr()> create_static_provider) {
  auto provider = create_static_provider();
  static_route_config_providers_.insert(provider.get());
  return provider;
}

RouteConfigProviderSharedPtr RouteConfigProviderManager::addDynamicProvider(
    const Protobuf::Message& rds, const std::string& route_config_name, Init::Manager& init_manager,
    std::function<
        std::pair<RouteConfigProviderSharedPtr, const Init::Target*>(uint64_t manager_identifier)>
        create_dynamic_provider) {

  // RdsRouteConfigSubscriptions are unique based on their serialized RDS config.
  uint64_t manager_identifier;
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.normalize_rds_provider_config")) {
    Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(rds);

    const Protobuf::FieldDescriptor* config_source_field =
        reflectable_message->GetDescriptor()->FindFieldByName("config_source");

    // If this assertion fails it means that dynamic provider config passed as 'rds' parameter does
    // not contain a proto field called 'config_source'. That field must have
    // envoy::config::core::v3::ConfigSource type.
    ASSERT(config_source_field != nullptr);

    Protobuf::Message* mutable_config_source = reflectable_message->GetReflection()->MutableMessage(
        reflectable_message, config_source_field);

    ASSERT(mutable_config_source != nullptr);

    envoy::config::core::v3::ConfigSource& config_source =
        dynamic_cast<envoy::config::core::v3::ConfigSource&>(*mutable_config_source);

    // Save the value of initial_fetch_timeout. Even though config 'rds' was passed as constant,
    // `reflectable_message` allows to modify values inside the 'rds'.
    // initial_fetch_timeout will be normalized (zeroed) before hash is calculated and must be
    // restored to the original value after hash has been calculated.
    Protobuf::Duration* orig_initial_timeout = config_source.release_initial_fetch_timeout();
    manager_identifier = MessageUtil::hash(rds);
    config_source.set_allocated_initial_fetch_timeout(orig_initial_timeout);
  } else {
    manager_identifier = MessageUtil::hash(rds);
  }

  auto existing_provider =
      reuseDynamicProvider(manager_identifier, init_manager, route_config_name);

  if (existing_provider) {
    return existing_provider;
  }
  auto new_provider = create_dynamic_provider(manager_identifier);
  init_manager.add(*new_provider.second);
  dynamic_route_config_providers_.insert({manager_identifier, new_provider});
  return new_provider.first;
}

RouteConfigProviderSharedPtr
RouteConfigProviderManager::reuseDynamicProvider(uint64_t manager_identifier,
                                                 Init::Manager& init_manager,
                                                 const std::string& route_config_name) {
  auto it = dynamic_route_config_providers_.find(manager_identifier);
  if (it == dynamic_route_config_providers_.end()) {
    return nullptr;
  }
  // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
  // in the RdsRouteConfigSubscription destructor, and the single threaded nature
  // of this code, locking the weak_ptr will not fail.
  auto existing_provider = it->second.first.lock();
  RELEASE_ASSERT(existing_provider != nullptr,
                 absl::StrCat("cannot find subscribed rds resource ", route_config_name));
  init_manager.add(*it->second.second);
  return existing_provider;
}

} // namespace Rds
} // namespace Envoy
