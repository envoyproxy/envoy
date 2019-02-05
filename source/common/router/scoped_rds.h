#pragma once

#include <string>

#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/config/config_provider_impl.h"

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
      const envoy::api::v2::ScopedRouteConfigurationsSet& config_proto,
      Server::Configuration::FactoryContext& factory_context,
      ScopedRoutesConfigProviderManager& config_provider_manager,
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds);

  ~InlineScopedRoutesConfigProvider() override = default;

  // Envoy::Config::ConfigProvider
  const Protobuf::Message* getConfigProto() const override { return &config_proto_; }
  std::string getConfigVersion() const override { return ""; }
  ConfigConstSharedPtr getConfig() const override { return config_; }

private:
  ConfigConstSharedPtr config_;
  const envoy::api::v2::ScopedRouteConfigurationsSet config_proto_;
  const envoy::config::filter::network::http_connection_manager::v2::Rds rds_;
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
      Envoy::Config::SubscriptionCallbacks<envoy::api::v2::ScopedRouteConfigurationsSet> {
public:
  ScopedRdsConfigSubscription(
      const envoy::config::filter::network::http_connection_manager::v2::ScopedRds& scoped_rds,
      const std::string& manager_identifier, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix, ScopedRoutesConfigProviderManager& config_provider_manager);

  ~ScopedRdsConfigSubscription() override = default;

  // Envoy::Config::ConfigSubscriptionInstanceBase
  void start() override { subscription_->start({scoped_routes_config_name_}, *this); }

  // Envoy::Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException*) override {
    ConfigSubscriptionInstanceBase::onConfigUpdateFailed();
  }
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::ScopedRouteConfigurationsSet>(resource).name();
  }

  const absl::optional<envoy::api::v2::ScopedRouteConfigurationsSet>& configProto() const {
    return scoped_routes_proto_;
  }

private:
  std::unique_ptr<Envoy::Config::Subscription<envoy::api::v2::ScopedRouteConfigurationsSet>>
      subscription_;
  const std::string scoped_routes_config_name_;
  Stats::ScopePtr scope_;
  ScopedRdsStats stats_;
  absl::optional<envoy::api::v2::ScopedRouteConfigurationsSet> scoped_routes_proto_;
};

using ScopedRdsConfigSubscriptionSharedPtr = std::shared_ptr<ScopedRdsConfigSubscription>;

// A ConfigProvider for scoped RDS that dynamically fetches scoped routing configuration via a
// subscription.
class ScopedRdsConfigProvider : public Envoy::Config::MutableConfigProviderImplBase {
public:
  ScopedRdsConfigProvider(
      ScopedRdsConfigSubscriptionSharedPtr&& subscription, ConfigConstSharedPtr initial_config,
      Server::Configuration::FactoryContext& factory_context,
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds);

  ~ScopedRdsConfigProvider() override = default;

  ScopedRdsConfigSubscription& subscription() { return *subscription_; }

  // Envoy::Config::MutableConfigProviderImplBase
  Envoy::Config::ConfigProvider::ConfigConstSharedPtr
  onConfigProtoUpdate(const Protobuf::Message& config) override;

  // Envoy::Config::ConfigProvider
  const Protobuf::Message* getConfigProto() const override {
    if (!subscription_->configProto().has_value()) {
      return nullptr;
    }
    return &subscription_->configProto().value();
  }
  std::string getConfigVersion() const override {
    if (subscription_->configInfo().has_value()) {
      return subscription_->configInfo().value().last_config_version_;
    }

    return "";
  }

private:
  ScopedRdsConfigSubscription* subscription_;
  const envoy::config::filter::network::http_connection_manager::v2::Rds rds_;
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
  Envoy::Config::ConfigProviderPtr
  createXdsConfigProvider(const Protobuf::Message& config_source_proto,
                          Server::Configuration::FactoryContext& factory_context,
                          const std::string& stat_prefix,
                          const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) override;
  Envoy::Config::ConfigProviderPtr createStaticConfigProvider(
      const Protobuf::Message& config_proto, Server::Configuration::FactoryContext& factory_context,
      const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) override;
};

// The optional argument passed to the ConfigProviderManager::create*() functions.
class ScopedRoutesConfigProviderManagerOptArg
    : public Envoy::Config::ConfigProviderManager::OptionalArg {
public:
  ScopedRoutesConfigProviderManagerOptArg(
      const envoy::config::filter::network::http_connection_manager::v2::Rds& rds)
      : rds_(rds) {}

  ~ScopedRoutesConfigProviderManagerOptArg() override = default;

  const envoy::config::filter::network::http_connection_manager::v2::Rds& rds_;
};

} // namespace Router
} // namespace Envoy
