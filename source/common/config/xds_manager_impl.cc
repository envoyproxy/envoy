#include "source/common/config/xds_manager_impl.h"

#include "envoy/config/core/v3/config_source.pb.validate.h"

#include "source/common/common/thread.h"
#include "source/common/config/custom_config_validators_impl.h"
#include "source/common/config/null_grpc_mux_impl.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Config {
namespace {
absl::Status createClients(Grpc::AsyncClientFactoryPtr& primary_factory,
                           Grpc::AsyncClientFactoryPtr& failover_factory,
                           Grpc::RawAsyncClientPtr& primary_client,
                           Grpc::RawAsyncClientPtr& failover_client) {
  absl::StatusOr<Grpc::RawAsyncClientPtr> success = primary_factory->createUncachedRawAsyncClient();
  RETURN_IF_NOT_OK_REF(success.status());
  primary_client = std::move(*success);
  if (failover_factory) {
    success = failover_factory->createUncachedRawAsyncClient();
    RETURN_IF_NOT_OK_REF(success.status());
    failover_client = std::move(*success);
  }
  return absl::OkStatus();
}
} // namespace

absl::Status XdsManagerImpl::initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                        Upstream::ClusterManager* cm) {
  ASSERT(cm != nullptr);
  cm_ = cm;

  // Initialize the XdsResourceDelegate extension, if set on the bootstrap config.
  if (bootstrap.has_xds_delegate_extension()) {
    auto& factory = Config::Utility::getAndCheckFactory<XdsResourcesDelegateFactory>(
        bootstrap.xds_delegate_extension());
    xds_resources_delegate_ = factory.createXdsResourcesDelegate(
        bootstrap.xds_delegate_extension().typed_config(),
        validation_context_.dynamicValidationVisitor(), api_, main_thread_dispatcher_);
  }

  // Initialize the XdsConfigTracker extension, if set on the bootstrap config.
  if (bootstrap.has_xds_config_tracker_extension()) {
    auto& tracker_factory = Config::Utility::getAndCheckFactory<XdsConfigTrackerFactory>(
        bootstrap.xds_config_tracker_extension());
    xds_config_tracker_ = tracker_factory.createXdsConfigTracker(
        bootstrap.xds_config_tracker_extension().typed_config(),
        validation_context_.dynamicValidationVisitor(), api_, main_thread_dispatcher_);
  }

  OptRef<XdsResourcesDelegate> xds_resources_delegate =
      makeOptRefFromPtr<XdsResourcesDelegate>(xds_resources_delegate_.get());
  OptRef<XdsConfigTracker> xds_config_tracker =
      makeOptRefFromPtr<XdsConfigTracker>(xds_config_tracker_.get());

  subscription_factory_ = std::make_unique<SubscriptionFactoryImpl>(
      local_info_, main_thread_dispatcher_, *cm_, validation_context_.dynamicValidationVisitor(),
      api_, server_, xds_resources_delegate, xds_config_tracker);
  return absl::OkStatus();
}

absl::Status
XdsManagerImpl::initializeAdsConnections(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  // Assumes that primary clusters were already initialized by the
  // cluster-manager.
  // Setup the xDS-TP based config-sources.
  // Iterate over the ConfigSources defined in the bootstrap and initialize each as an ADS source.
  for (const auto& config_source : bootstrap.config_sources()) {
    absl::StatusOr<AuthorityData> authority_or_error = createAuthority(config_source, false);
    RETURN_IF_NOT_OK(authority_or_error.status());
    authorities_.emplace_back(std::move(*authority_or_error));
  }
  // Initialize the default_config_source as an ADS source.
  if (bootstrap.has_default_config_source()) {
    absl::StatusOr<AuthorityData> authority_or_error =
        createAuthority(bootstrap.default_config_source(), true);
    RETURN_IF_NOT_OK(authority_or_error.status());
    default_authority_ = std::make_unique<AuthorityData>(std::move(*authority_or_error));
  }

  // TODO(adisuissa): the rest of this function should be refactored so the shared
  // code with "createAuthority" is only defined once.
  // Setup the ads_config mux.
  const auto& dyn_resources = bootstrap.dynamic_resources();
  // This is the only point where distinction between delta ADS and state-of-the-world ADS is made.
  // After here, we just have a GrpcMux interface held in ads_mux_, which hides
  // whether the backing implementation is delta or SotW.
  if (dyn_resources.has_ads_config()) {
    Config::CustomConfigValidatorsPtr custom_config_validators =
        std::make_unique<Config::CustomConfigValidatorsImpl>(
            validation_context_.dynamicValidationVisitor(), server_,
            dyn_resources.ads_config().config_validators());

    auto strategy_or_error = Config::Utility::prepareJitteredExponentialBackOffStrategy(
        dyn_resources.ads_config(), random_,
        Envoy::Config::SubscriptionFactory::RetryInitialDelayMs,
        Envoy::Config::SubscriptionFactory::RetryMaxDelayMs);
    RETURN_IF_NOT_OK_REF(strategy_or_error.status());
    JitteredExponentialBackOffStrategyPtr backoff_strategy = std::move(strategy_or_error.value());

    const bool use_eds_cache =
        Runtime::runtimeFeatureEnabled("envoy.restart_features.use_eds_cache_for_ads");

    OptRef<XdsConfigTracker> xds_config_tracker =
        makeOptRefFromPtr<XdsConfigTracker>(xds_config_tracker_.get());

    if (dyn_resources.ads_config().api_type() ==
        envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
      absl::Status status = Config::Utility::checkTransportVersion(dyn_resources.ads_config());
      RETURN_IF_NOT_OK(status);
      std::string name;
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        name = "envoy.config_mux.delta_grpc_mux_factory";
      } else {
        name = "envoy.config_mux.new_grpc_mux_factory";
      }
      auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(name);
      if (!factory) {
        return absl::InvalidArgumentError(fmt::format("{} not found", name));
      }
      auto factory_primary_or_error = Config::Utility::factoryForGrpcApiConfigSource(
          cm_->grpcAsyncClientManager(), dyn_resources.ads_config(), *stats_.rootScope(), false, 0,
          false);
      RETURN_IF_NOT_OK_REF(factory_primary_or_error.status());
      Grpc::AsyncClientFactoryPtr factory_failover = nullptr;
      if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
        auto factory_failover_or_error = Config::Utility::factoryForGrpcApiConfigSource(
            cm_->grpcAsyncClientManager(), dyn_resources.ads_config(), *stats_.rootScope(), false,
            1, false);
        RETURN_IF_NOT_OK_REF(factory_failover_or_error.status());
        factory_failover = std::move(factory_failover_or_error.value());
      }
      Grpc::RawAsyncClientPtr primary_client;
      Grpc::RawAsyncClientPtr failover_client;
      RETURN_IF_NOT_OK(createClients(factory_primary_or_error.value(), factory_failover,
                                     primary_client, failover_client));
      ads_mux_ = factory->create(std::move(primary_client), std::move(failover_client),
                                 main_thread_dispatcher_, random_, *stats_.rootScope(),
                                 dyn_resources.ads_config(), local_info_,
                                 std::move(custom_config_validators), std::move(backoff_strategy),
                                 xds_config_tracker, {}, use_eds_cache);
    } else {
      absl::Status status = Config::Utility::checkTransportVersion(dyn_resources.ads_config());
      RETURN_IF_NOT_OK(status);
      std::string name;
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        name = "envoy.config_mux.sotw_grpc_mux_factory";
      } else {
        name = "envoy.config_mux.grpc_mux_factory";
      }

      auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(name);
      if (!factory) {
        return absl::InvalidArgumentError(fmt::format("{} not found", name));
      }
      auto factory_primary_or_error = Config::Utility::factoryForGrpcApiConfigSource(
          cm_->grpcAsyncClientManager(), dyn_resources.ads_config(), *stats_.rootScope(), false, 0,
          false);
      RETURN_IF_NOT_OK_REF(factory_primary_or_error.status());
      Grpc::AsyncClientFactoryPtr factory_failover = nullptr;
      if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
        auto factory_failover_or_error = Config::Utility::factoryForGrpcApiConfigSource(
            cm_->grpcAsyncClientManager(), dyn_resources.ads_config(), *stats_.rootScope(), false,
            1, false);
        RETURN_IF_NOT_OK_REF(factory_failover_or_error.status());
        factory_failover = std::move(factory_failover_or_error.value());
      }
      OptRef<XdsResourcesDelegate> xds_resources_delegate =
          makeOptRefFromPtr<XdsResourcesDelegate>(xds_resources_delegate_.get());
      Grpc::RawAsyncClientPtr primary_client;
      Grpc::RawAsyncClientPtr failover_client;
      RETURN_IF_NOT_OK(createClients(factory_primary_or_error.value(), factory_failover,
                                     primary_client, failover_client));
      ads_mux_ = factory->create(std::move(primary_client), std::move(failover_client),
                                 main_thread_dispatcher_, random_, *stats_.rootScope(),
                                 dyn_resources.ads_config(), local_info_,
                                 std::move(custom_config_validators), std::move(backoff_strategy),
                                 xds_config_tracker, xds_resources_delegate, use_eds_cache);
    }
  } else {
    ads_mux_ = std::make_unique<Config::NullGrpcMuxImpl>();
  }
  return absl::OkStatus();
}

absl::Status
XdsManagerImpl::setAdsConfigSource(const envoy::config::core::v3::ApiConfigSource& config_source) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  RETURN_IF_NOT_OK(validateAdsConfig(config_source));

  return replaceAdsMux(config_source);
}

absl::StatusOr<XdsManagerImpl::AuthorityData>
XdsManagerImpl::createAuthority(const envoy::config::core::v3::ConfigSource& config_source,
                                bool allow_no_authority_names) {
  // Only the config_source.api_config_source can be used for authorities at the moment.
  if (!config_source.has_api_config_source()) {
    return absl::InvalidArgumentError(
        "Only api_config_source type is currently supported for xdstp-based config sources.");
  }

  if (!allow_no_authority_names && config_source.authorities().empty()) {
    return absl::InvalidArgumentError(
        "xdstp-based non-default config source must have at least one authority.");
  }

  // Validate that the authority names in the config source don't have repeated values.
  absl::flat_hash_set<std::string> config_source_authorities;
  config_source_authorities.reserve(config_source.authorities().size());
  for (const auto& authority : config_source.authorities()) {
    const auto ret = config_source_authorities.emplace(authority.name());
    if (!ret.second) {
      return absl::InvalidArgumentError(
          fmt::format("xdstp-based config source authority {} is configured more than once in an "
                      "xdstp-based config source.",
                      authority.name()));
    }
  }

  const auto& api_config_source = config_source.api_config_source();

  if ((api_config_source.api_type() != envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC) &&
      (api_config_source.api_type() !=
       envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC)) {
    return absl::InvalidArgumentError("xdstp-based config source authority only supports "
                                      "AGGREGATED_GRPC and AGGREGATED_DELTA_GRPC types.");
  }

  Config::CustomConfigValidatorsPtr custom_config_validators =
      std::make_unique<Config::CustomConfigValidatorsImpl>(
          validation_context_.dynamicValidationVisitor(), server_,
          api_config_source.config_validators());

  auto strategy_or_error = Config::Utility::prepareJitteredExponentialBackOffStrategy(
      api_config_source, random_, Envoy::Config::SubscriptionFactory::RetryInitialDelayMs,
      Envoy::Config::SubscriptionFactory::RetryMaxDelayMs);
  RETURN_IF_NOT_OK_REF(strategy_or_error.status());
  JitteredExponentialBackOffStrategyPtr backoff_strategy = std::move(strategy_or_error.value());

  const bool use_eds_cache =
      Runtime::runtimeFeatureEnabled("envoy.restart_features.use_eds_cache_for_ads");

  OptRef<XdsConfigTracker> xds_config_tracker =
      makeOptRefFromPtr<XdsConfigTracker>(xds_config_tracker_.get());

  GrpcMuxSharedPtr authority_mux = nullptr;
  if (api_config_source.api_type() ==
      envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC) {
    absl::Status status = Config::Utility::checkTransportVersion(api_config_source);
    RETURN_IF_NOT_OK(status);
    std::string name;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
      name = "envoy.config_mux.delta_grpc_mux_factory";
    } else {
      name = "envoy.config_mux.new_grpc_mux_factory";
    }
    auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(name);
    if (!factory) {
      return absl::InvalidArgumentError(fmt::format("{} not found", name));
    }
    auto factory_primary_or_error = Config::Utility::factoryForGrpcApiConfigSource(
        cm_->grpcAsyncClientManager(), api_config_source, *stats_.rootScope(), false, 0, true);
    RETURN_IF_NOT_OK_REF(factory_primary_or_error.status());
    Grpc::AsyncClientFactoryPtr factory_failover = nullptr;
    if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
      auto factory_failover_or_error = Config::Utility::factoryForGrpcApiConfigSource(
          cm_->grpcAsyncClientManager(), api_config_source, *stats_.rootScope(), false, 1, true);
      RETURN_IF_NOT_OK_REF(factory_failover_or_error.status());
      factory_failover = std::move(factory_failover_or_error.value());
    }
    Grpc::RawAsyncClientPtr primary_client;
    Grpc::RawAsyncClientPtr failover_client;
    RETURN_IF_NOT_OK(createClients(factory_primary_or_error.value(), factory_failover,
                                   primary_client, failover_client));
    authority_mux = factory->create(
        std::move(primary_client), std::move(failover_client), main_thread_dispatcher_, random_,
        *stats_.rootScope(), api_config_source, local_info_, std::move(custom_config_validators),
        std::move(backoff_strategy), xds_config_tracker, {}, use_eds_cache);
  } else {
    ASSERT(api_config_source.api_type() ==
           envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC);
    absl::Status status = Config::Utility::checkTransportVersion(api_config_source);
    RETURN_IF_NOT_OK(status);
    std::string name;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
      name = "envoy.config_mux.sotw_grpc_mux_factory";
    } else {
      name = "envoy.config_mux.grpc_mux_factory";
    }

    auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(name);
    if (!factory) {
      return absl::InvalidArgumentError(fmt::format("{} not found", name));
    }
    auto factory_primary_or_error = Config::Utility::factoryForGrpcApiConfigSource(
        cm_->grpcAsyncClientManager(), api_config_source, *stats_.rootScope(), false, 0, true);
    RETURN_IF_NOT_OK_REF(factory_primary_or_error.status());
    Grpc::AsyncClientFactoryPtr factory_failover = nullptr;
    if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
      auto factory_failover_or_error = Config::Utility::factoryForGrpcApiConfigSource(
          cm_->grpcAsyncClientManager(), api_config_source, *stats_.rootScope(), false, 1, true);
      RETURN_IF_NOT_OK_REF(factory_failover_or_error.status());
      factory_failover = std::move(factory_failover_or_error.value());
    }
    OptRef<XdsResourcesDelegate> xds_resources_delegate =
        makeOptRefFromPtr<XdsResourcesDelegate>(xds_resources_delegate_.get());
    Grpc::RawAsyncClientPtr primary_client;
    Grpc::RawAsyncClientPtr failover_client;
    RETURN_IF_NOT_OK(createClients(factory_primary_or_error.value(), factory_failover,
                                   primary_client, failover_client));
    authority_mux = factory->create(
        std::move(primary_client), std::move(failover_client), main_thread_dispatcher_, random_,
        *stats_.rootScope(), api_config_source, local_info_, std::move(custom_config_validators),
        std::move(backoff_strategy), xds_config_tracker, xds_resources_delegate, use_eds_cache);
  }
  ASSERT(authority_mux != nullptr);

  return AuthorityData(std::move(config_source_authorities), std::move(authority_mux));
}

absl::Status
XdsManagerImpl::validateAdsConfig(const envoy::config::core::v3::ApiConfigSource& config_source) {
  auto& validation_visitor = validation_context_.staticValidationVisitor();
  TRY_ASSERT_MAIN_THREAD { MessageUtil::validate(config_source, validation_visitor); }
  END_TRY
  CATCH(const EnvoyException& e, { return absl::InternalError(e.what()); });
  return absl::OkStatus();
}

absl::Status
XdsManagerImpl::replaceAdsMux(const envoy::config::core::v3::ApiConfigSource& ads_config) {
  ASSERT(cm_ != nullptr);
  // If there was no ADS defined, reject replacement.
  const auto& bootstrap = server_.bootstrap();
  if (!bootstrap.has_dynamic_resources() || !bootstrap.dynamic_resources().has_ads_config()) {
    return absl::InternalError(
        "Cannot replace an ADS config when one wasn't previously configured in the bootstrap");
  }
  const auto& bootstrap_ads_config = server_.bootstrap().dynamic_resources().ads_config();

  // There is no support for switching between different ADS types.
  if (ads_config.api_type() != bootstrap_ads_config.api_type()) {
    return absl::InternalError(fmt::format(
        "Cannot replace an ADS config with a different api_type (expected: {})",
        envoy::config::core::v3::ApiConfigSource::ApiType_Name(bootstrap_ads_config.api_type())));
  }

  // There is no support for using a different config validator. Note that if
  // this is mainly because the validator could be stateful and if the delta-xDS
  // protocol is used, then the new validator will not have the context of the
  // previous one.
  if (bootstrap_ads_config.config_validators_size() != ads_config.config_validators_size()) {
    return absl::InternalError(fmt::format(
        "Cannot replace config_validators in ADS config (different size) - Previous: {}, New: {}",
        bootstrap_ads_config.config_validators_size(), ads_config.config_validators_size()));
  } else if (bootstrap_ads_config.config_validators_size() > 0) {
    const bool equal_config_validators = std::equal(
        bootstrap_ads_config.config_validators().begin(),
        bootstrap_ads_config.config_validators().end(), ads_config.config_validators().begin(),
        [](const envoy::config::core::v3::TypedExtensionConfig& a,
           const envoy::config::core::v3::TypedExtensionConfig& b) {
          return Protobuf::util::MessageDifferencer::Equivalent(a, b);
        });
    if (!equal_config_validators) {
      return absl::InternalError(fmt::format("Cannot replace config_validators in ADS config "
                                             "(different contents)\nPrevious: {}\nNew: {}",
                                             bootstrap_ads_config.DebugString(),
                                             ads_config.DebugString()));
    }
  }

  ENVOY_LOG_MISC(trace, "Replacing ADS config with:\n{}", ads_config.DebugString());
  auto strategy_or_error = Config::Utility::prepareJitteredExponentialBackOffStrategy(
      ads_config, random_, Envoy::Config::SubscriptionFactory::RetryInitialDelayMs,
      Envoy::Config::SubscriptionFactory::RetryMaxDelayMs);
  RETURN_IF_NOT_OK_REF(strategy_or_error.status());
  JitteredExponentialBackOffStrategyPtr backoff_strategy = std::move(strategy_or_error.value());

  absl::Status status = Config::Utility::checkTransportVersion(ads_config);
  RETURN_IF_NOT_OK(status);

  auto factory_primary_or_error = Config::Utility::factoryForGrpcApiConfigSource(
      cm_->grpcAsyncClientManager(), ads_config, *stats_.rootScope(), false, 0, false);
  RETURN_IF_NOT_OK_REF(factory_primary_or_error.status());
  Grpc::AsyncClientFactoryPtr factory_failover = nullptr;
  if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
    auto factory_failover_or_error = Config::Utility::factoryForGrpcApiConfigSource(
        cm_->grpcAsyncClientManager(), ads_config, *stats_.rootScope(), false, 1, false);
    RETURN_IF_NOT_OK_REF(factory_failover_or_error.status());
    factory_failover = std::move(factory_failover_or_error.value());
  }
  Grpc::RawAsyncClientPtr primary_client;
  Grpc::RawAsyncClientPtr failover_client;
  RETURN_IF_NOT_OK(createClients(factory_primary_or_error.value(), factory_failover, primary_client,
                                 failover_client));

  // Primary client must not be null, as the primary xDS source must be a valid one.
  // The failover_client may be null (no failover defined).
  ASSERT(primary_client != nullptr);

  // This will cause a disconnect from the current sources, and replacement of the clients.
  status = ads_mux_->updateMuxSource(std::move(primary_client), std::move(failover_client),
                                     *stats_.rootScope(), std::move(backoff_strategy), ads_config);
  return status;
}

} // namespace Config
} // namespace Envoy
