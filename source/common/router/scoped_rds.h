#pragma once

#include <string>

#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/config/config_provider_impl.h"
#include "common/router/scoped_config_impl.h"

namespace Envoy {
namespace Router {

// Scoped routing configuration utilities.
class ScopedRoutesConfigProviderUtil {
public:
  // If enabled in the HttpConnectionManager config, returns a ConfigProvider for scoped routing
  // configuration.
  static Envoy::Config::ConfigProviderPtr maybeCreate(
      const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
          config,
      Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
      Envoy::Config::ConfigProviderManager& scoped_routes_config_provider_manager);
};

class ScopedRoutesConfigProviderManager;

// A ConfigProvider for inline scoped routing configuration.
class InlineScopedRoutesConfigProvider : public Envoy::Config::ImmutableConfigProviderImplBase {
public:
  InlineScopedRoutesConfigProvider(
      std::vector<std::unique_ptr<const Protobuf::Message>>&& config_protos,
      const std::string& name, Server::Configuration::FactoryContext& factory_context,
      ScopedRoutesConfigProviderManager& config_provider_manager,
      const envoy::api::v2::core::ConfigSource& rds_config_source,
      const envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::
          ScopeKeyBuilder& scope_key_builder);

  ~InlineScopedRoutesConfigProvider() override = default;

  const std::string& name() const { return name_; }

  // Envoy::Config::ConfigProvider
  const Envoy::Config::ConfigProvider::ConfigProtoVector getConfigProtos() const override {
    Envoy::Config::ConfigProvider::ConfigProtoVector out_protos;
    std::for_each(config_protos_.begin(), config_protos_.end(),
                  [&out_protos](const std::unique_ptr<const Protobuf::Message>& message) {
                    out_protos.push_back(message.get());
                  });
    return out_protos;
  }

  std::string getConfigVersion() const override { return ""; }
  ConfigConstSharedPtr getConfig() const override { return config_; }

private:
  const std::string name_;
  ConfigConstSharedPtr config_;
  const std::vector<std::unique_ptr<const Protobuf::Message>> config_protos_;
  const envoy::api::v2::core::ConfigSource rds_config_source_;
};

/**
 * All SRDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_SCOPED_RDS_STATS(COUNTER)                                                              \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)

// clang-format on

struct ScopedRdsStats {
  ALL_SCOPED_RDS_STATS(GENERATE_COUNTER_STRUCT)
};

// A scoped RDS subscription to be used with the dynamic scoped RDS ConfigProvider.
class ScopedRdsConfigSubscription
    : public Envoy::Config::ConfigSubscriptionInstanceBase,
      Envoy::Config::SubscriptionCallbacks<envoy::api::v2::ScopedRouteConfiguration> {
public:
  using ScopedRouteConfigurationMap =
      std::map<std::string, envoy::api::v2::ScopedRouteConfiguration>;

  ScopedRdsConfigSubscription(
      const envoy::config::filter::network::http_connection_manager::v2::ScopedRds& scoped_rds,
      const std::string& manager_identifier, const std::string& name,
      Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
      ScopedRoutesConfigProviderManager& config_provider_manager);

  ~ScopedRdsConfigSubscription() override = default;

  const std::string& name() const { return name_; }

  // Envoy::Config::ConfigSubscriptionInstanceBase
  void start() override { subscription_->start({}, *this); }

  // Envoy::Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException*) override {
    ConfigSubscriptionInstanceBase::onConfigUpdateFailed();
  }
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::ScopedRouteConfiguration>(resource).name();
  }
  const ScopedConfigManager::ScopedRouteMap& scopedRouteMap() const {
    return scoped_config_manager_.scopedRouteMap();
  }

private:
  const std::string name_;
  std::unique_ptr<Envoy::Config::Subscription<envoy::api::v2::ScopedRouteConfiguration>>
      subscription_;
  Stats::ScopePtr scope_;
  ScopedRdsStats stats_;
  ScopedConfigManager scoped_config_manager_;
};

using ScopedRdsConfigSubscriptionSharedPtr = std::shared_ptr<ScopedRdsConfigSubscription>;

// A ConfigProvider for scoped RDS that dynamically fetches scoped routing configuration via a
// subscription.
class ScopedRdsConfigProvider : public Envoy::Config::MutableConfigProviderImplBase {
public:
  ScopedRdsConfigProvider(ScopedRdsConfigSubscriptionSharedPtr&& subscription,
                          Server::Configuration::FactoryContext& factory_context,
                          const envoy::api::v2::core::ConfigSource& rds_config_source,
                          const envoy::config::filter::network::http_connection_manager::v2::
                              ScopedRoutes::ScopeKeyBuilder& scope_key_builder);

  ScopedRdsConfigSubscription& subscription() { return *subscription_; }

  // Envoy::Config::MutableConfigProviderImplBase
  Envoy::Config::ConfigProvider::ConfigConstSharedPtr
  onConfigProtoUpdate(const Protobuf::Message&) override {
    return nullptr;
  }

  // Envoy::Config::ConfigProvider
  const Envoy::Config::ConfigProvider::ConfigProtoVector getConfigProtos() const override {
    const ScopedConfigManager::ScopedRouteMap& scoped_route_map = subscription_->scopedRouteMap();
    if (scoped_route_map.empty()) {
      return {};
    }

    Envoy::Config::ConfigProvider::ConfigProtoVector config_protos(scoped_route_map.size());
    for (ScopedConfigManager::ScopedRouteMap::const_iterator it = scoped_route_map.begin();
         it != scoped_route_map.end(); ++it) {
      config_protos.push_back(&it->second->config_proto_);
    }
    return config_protos;
  }
  std::string getConfigVersion() const override {
    if (subscription_->configInfo().has_value()) {
      return subscription_->configInfo().value().last_config_version_;
    }

    return "";
  }
  ConfigConstSharedPtr getConfig() const override {
    return std::dynamic_pointer_cast<const Envoy::Config::ConfigProvider::Config>(tls()->get());
  }

private:
  ScopedRdsConfigSubscription* subscription_;
  const envoy::api::v2::core::ConfigSource rds_config_source_;
};

// A ConfigProviderManager for scoped routing configuration that creates static/inline and dynamic
// (xds) config providers.
class ScopedRoutesConfigProviderManager : public Envoy::Config::ConfigProviderManagerImplBase {
public:
  ScopedRoutesConfigProviderManager(Server::Admin& admin)
      : Envoy::Config::ConfigProviderManagerImplBase(admin, "route_scopes") {}

  ~ScopedRoutesConfigProviderManager() override = default;

  // Envoy::Config::ConfigProviderManagerImplBase
  ProtobufTypes::MessagePtr dumpConfigs() const override;

  // Envoy::Config::ConfigProviderManager
  Envoy::Config::ConfigProviderPtr
  createXdsConfigProvider(const Protobuf::Message& config_source_proto,
                          Server::Configuration::FactoryContext& factory_context,
                          const std::string& stat_prefix,
                          const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) override;
  Envoy::Config::ConfigProviderPtr
  createStaticConfigProvider(const Protobuf::Message&, Server::Configuration::FactoryContext&,
                             const Envoy::Config::ConfigProviderManager::OptionalArg&) override {
    ASSERT(false ||
           "SRDS supports delta updates and requires the use of the createStaticConfigProvider() "
           "overload that accepts a config proto set as an argument.");
    return nullptr;
  }
  Envoy::Config::ConfigProviderPtr createStaticConfigProvider(
      std::vector<std::unique_ptr<const Protobuf::Message>>&& config_protos,
      Server::Configuration::FactoryContext& factory_context,
      const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) override;
};

// The optional argument passed to the ConfigProviderManager::create*() functions.
class ScopedRoutesConfigProviderManagerOptArg
    : public Envoy::Config::ConfigProviderManager::OptionalArg {
public:
  ScopedRoutesConfigProviderManagerOptArg(
      const std::string& scoped_routes_name,
      const envoy::api::v2::core::ConfigSource& rds_config_source,
      const envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::
          ScopeKeyBuilder& scope_key_builder)
      : scoped_routes_name_(scoped_routes_name), rds_config_source_(rds_config_source),
        scope_key_builder_(scope_key_builder) {}

  const std::string scoped_routes_name_;
  const envoy::api::v2::core::ConfigSource& rds_config_source_;
  const envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder&
      scope_key_builder_;
};

} // namespace Router
} // namespace Envoy
