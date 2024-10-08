#include "source/common/config/utility.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Config {

std::string Utility::truncateGrpcStatusMessage(absl::string_view error_message) {
  // GRPC sends error message via trailers, which by default has a 8KB size limit(see
  // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#requests). Truncates the
  // error message if it's too long.
  constexpr uint32_t kProtobufErrMsgLen = 4096;
  return fmt::format("{}{}", error_message.substr(0, kProtobufErrMsgLen),
                     error_message.length() > kProtobufErrMsgLen ? "...(truncated)" : "");
}

absl::StatusOr<Upstream::ClusterConstOptRef> Utility::checkCluster(absl::string_view error_prefix,
                                                                   absl::string_view cluster_name,
                                                                   Upstream::ClusterManager& cm,
                                                                   bool allow_added_via_api) {
  const auto cluster = cm.clusters().getCluster(cluster_name);
  if (!cluster.has_value()) {
    return absl::InvalidArgumentError(
        fmt::format("{}: unknown cluster '{}'", error_prefix, cluster_name));
  }

  if (!allow_added_via_api && cluster->get().info()->addedViaApi()) {
    return absl::InvalidArgumentError(fmt::format(
        "{}: invalid cluster '{}': currently only static (non-CDS) clusters are supported",
        error_prefix, cluster_name));
  }
  return cluster;
}

absl::Status Utility::checkLocalInfo(absl::string_view error_prefix,
                                     const LocalInfo::LocalInfo& local_info) {
  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    return absl::InvalidArgumentError(
        fmt::format("{}: node 'id' and 'cluster' are required. Set it either in 'node' config or "
                    "via --service-node and --service-cluster options.",
                    error_prefix, local_info.node().DebugString()));
  }
  return absl::OkStatus();
}

absl::Status Utility::checkFilesystemSubscriptionBackingPath(const std::string& path,
                                                             Api::Api& api) {
  // TODO(junr03): the file might be deleted between this check and the
  // watch addition.
  if (!api.fileSystem().fileExists(path)) {
    return absl::InvalidArgumentError(fmt::format(
        "paths must refer to an existing path in the system: '{}' does not exist", path));
  }
  return absl::OkStatus();
}

namespace {
/**
 * Check the grpc_services and cluster_names for API config sanity. Throws on error.
 * @param api_config_source the config source to validate.
 * @param max_grpc_services the maximal number of grpc services allowed.
 * @return an invalid status when an API config has the wrong number of gRPC
 * services or cluster names, depending on expectations set by its API type.
 */
absl::Status
checkApiConfigSourceNames(const envoy::config::core::v3::ApiConfigSource& api_config_source,
                          int max_grpc_services) {
  const bool is_grpc =
      (api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::GRPC ||
       api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);

  if (api_config_source.cluster_names().empty() && api_config_source.grpc_services().empty()) {
    return absl::InvalidArgumentError(
        fmt::format("API configs must have either a gRPC service or a cluster name defined: {}",
                    api_config_source.DebugString()));
  }

  if (is_grpc) {
    if (!api_config_source.cluster_names().empty()) {
      return absl::InvalidArgumentError(
          fmt::format("{}::(DELTA_)GRPC must not have a cluster name specified: {}",
                      api_config_source.GetTypeName(), api_config_source.DebugString()));
    }
    if (api_config_source.grpc_services_size() > max_grpc_services) {
      return absl::InvalidArgumentError(fmt::format(
          "{}::(DELTA_)GRPC must have no more than {} gRPC services specified: {}",
          api_config_source.GetTypeName(), max_grpc_services, api_config_source.DebugString()));
    }
  } else {
    if (!api_config_source.grpc_services().empty()) {
      return absl::InvalidArgumentError(
          fmt::format("{}, if not a gRPC type, must not have a gRPC service specified: {}",
                      api_config_source.GetTypeName(), api_config_source.DebugString()));
    }
    if (api_config_source.cluster_names().size() != 1) {
      return absl::InvalidArgumentError(
          fmt::format("{} must have a singleton cluster name specified: {}",
                      api_config_source.GetTypeName(), api_config_source.DebugString()));
    }
  }
  return absl::OkStatus();
}
} // namespace

absl::Status
Utility::validateClusterName(const Upstream::ClusterManager::ClusterSet& primary_clusters,
                             const std::string& cluster_name, const std::string& config_source) {
  const auto& it = primary_clusters.find(cluster_name);
  if (it == primary_clusters.end()) {
    return absl::InvalidArgumentError(
        fmt::format("{} must have a statically defined non-EDS cluster: '{}' does "
                    "not exist, was added via api, or is an EDS cluster",
                    config_source, cluster_name));
  }
  return absl::OkStatus();
}

absl::Status Utility::checkApiConfigSourceSubscriptionBackingCluster(
    const Upstream::ClusterManager::ClusterSet& primary_clusters,
    const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  // We don't need to check backing sources for ADS sources, the backing cluster must be verified in
  // the ads_config.
  if (api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC ||
      api_config_source.api_type() ==
          envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC) {
    return absl::OkStatus();
  }
  RETURN_IF_NOT_OK(checkApiConfigSourceNames(
      api_config_source,
      Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support") ? 2 : 1));

  const bool is_grpc =
      (api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::GRPC);

  if (!api_config_source.cluster_names().empty()) {
    // All API configs of type REST and UNSUPPORTED_REST_LEGACY should have cluster names.
    // Additionally, some gRPC API configs might have a cluster name set instead
    // of an envoy gRPC.
    RETURN_IF_NOT_OK(Utility::validateClusterName(
        primary_clusters, api_config_source.cluster_names()[0], api_config_source.GetTypeName()));
  } else if (is_grpc) {
    // Some ApiConfigSources of type GRPC won't have a cluster name, such as if
    // they've been configured with google_grpc.
    if (api_config_source.grpc_services()[0].has_envoy_grpc()) {
      // If an Envoy gRPC exists, we take its cluster name.
      RETURN_IF_NOT_OK(Utility::validateClusterName(
          primary_clusters, api_config_source.grpc_services()[0].envoy_grpc().cluster_name(),
          api_config_source.GetTypeName()));
    }
    if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support") &&
        api_config_source.grpc_services_size() == 2 &&
        api_config_source.grpc_services()[1].has_envoy_grpc()) {
      // If an Envoy failover gRPC exists, we validate its cluster name.
      RETURN_IF_NOT_OK(Utility::validateClusterName(
          primary_clusters, api_config_source.grpc_services()[1].envoy_grpc().cluster_name(),
          api_config_source.GetTypeName()));
    }
  }
  // Otherwise, there is no cluster name to validate.
  return absl::OkStatus();
}

absl::optional<std::string>
Utility::getGrpcControlPlane(const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  if (api_config_source.grpc_services_size() > 0) {
    std::string res = "";
    // In case more than one grpc service is defined, concatenate the names for
    // a unique GrpcControlPlane identifier.
    if (api_config_source.grpc_services(0).has_envoy_grpc()) {
      res = api_config_source.grpc_services(0).envoy_grpc().cluster_name();
    } else if (api_config_source.grpc_services(0).has_google_grpc()) {
      res = api_config_source.grpc_services(0).google_grpc().target_uri();
    }
    // Concatenate the failover gRPC service.
    if (api_config_source.grpc_services_size() == 2) {
      if (api_config_source.grpc_services(1).has_envoy_grpc()) {
        absl::StrAppend(&res, ",", api_config_source.grpc_services(1).envoy_grpc().cluster_name());
      } else if (api_config_source.grpc_services(1).has_google_grpc()) {
        absl::StrAppend(&res, ",", api_config_source.grpc_services(1).google_grpc().target_uri());
      }
    }
    return res;
  }
  return absl::nullopt;
}

std::chrono::milliseconds Utility::configSourceInitialFetchTimeout(
    const envoy::config::core::v3::ConfigSource& config_source) {
  return std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(config_source, initial_fetch_timeout, 15000));
}

absl::StatusOr<RateLimitSettings>
Utility::parseRateLimitSettings(const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  RateLimitSettings rate_limit_settings;
  if (api_config_source.has_rate_limit_settings()) {
    rate_limit_settings.enabled_ = true;
    rate_limit_settings.max_tokens_ =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(api_config_source.rate_limit_settings(), max_tokens,
                                        Envoy::Config::RateLimitSettings::DefaultMaxTokens);
    rate_limit_settings.fill_rate_ =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(api_config_source.rate_limit_settings(), fill_rate,
                                        Envoy::Config::RateLimitSettings::DefaultFillRate);
    // Reject the NaN and Inf values.
    if (std::isnan(rate_limit_settings.fill_rate_) || std::isinf(rate_limit_settings.fill_rate_)) {
      return absl::InvalidArgumentError(
          fmt::format("The value of fill_rate in RateLimitSettings ({}) must not be NaN nor Inf",
                      rate_limit_settings.fill_rate_));
    }
  }
  return rate_limit_settings;
}

absl::StatusOr<Grpc::AsyncClientFactoryPtr> Utility::factoryForGrpcApiConfigSource(
    Grpc::AsyncClientManager& async_client_manager,
    const envoy::config::core::v3::ApiConfigSource& api_config_source, Stats::Scope& scope,
    bool skip_cluster_check, int grpc_service_idx) {
  RETURN_IF_NOT_OK(checkApiConfigSourceNames(
      api_config_source,
      Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support") ? 2 : 1));

  if (api_config_source.api_type() != envoy::config::core::v3::ApiConfigSource::GRPC &&
      api_config_source.api_type() != envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
    return absl::InvalidArgumentError(fmt::format("{} type must be gRPC: {}",
                                                  api_config_source.GetTypeName(),
                                                  api_config_source.DebugString()));
  }

  if (grpc_service_idx >= api_config_source.grpc_services_size()) {
    // No returned factory in case there's no entry.
    return nullptr;
  }

  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.MergeFrom(api_config_source.grpc_services(grpc_service_idx));

  return async_client_manager.factoryForGrpcService(grpc_service, scope, skip_cluster_check);
}

void Utility::translateOpaqueConfig(const ProtobufWkt::Any& typed_config,
                                    ProtobufMessage::ValidationVisitor& validation_visitor,
                                    Protobuf::Message& out_proto) {
  static const std::string struct_type = ProtobufWkt::Struct::default_instance().GetTypeName();
  static const std::string typed_struct_type =
      xds::type::v3::TypedStruct::default_instance().GetTypeName();
  static const std::string legacy_typed_struct_type =
      udpa::type::v1::TypedStruct::default_instance().GetTypeName();
  if (!typed_config.value().empty()) {
    // Unpack methods will only use the fully qualified type name after the last '/'.
    // https://github.com/protocolbuffers/protobuf/blob/3.6.x/src/google/protobuf/any.proto#L87
    absl::string_view type = TypeUtil::typeUrlToDescriptorFullName(typed_config.type_url());

    if (type == typed_struct_type) {
      xds::type::v3::TypedStruct typed_struct;
      MessageUtil::unpackToOrThrow(typed_config, typed_struct);
      // if out_proto is expecting Struct, return directly
      if (out_proto.GetTypeName() == struct_type) {
        out_proto.CheckTypeAndMergeFrom(typed_struct.value());
      } else {
        // The typed struct might match out_proto, or some earlier version, let
        // MessageUtil::jsonConvert sort this out.
#ifdef ENVOY_ENABLE_YAML
        MessageUtil::jsonConvert(typed_struct.value(), validation_visitor, out_proto);
#else
        IS_ENVOY_BUG("Attempting to use JSON typed structs with JSON compiled out");
#endif
      }
    } else if (type == legacy_typed_struct_type) {
      udpa::type::v1::TypedStruct typed_struct;
      MessageUtil::unpackToOrThrow(typed_config, typed_struct);
      // if out_proto is expecting Struct, return directly
      if (out_proto.GetTypeName() == struct_type) {
        out_proto.CheckTypeAndMergeFrom(typed_struct.value());
      } else {
        // The typed struct might match out_proto, or some earlier version, let
        // MessageUtil::jsonConvert sort this out.
#ifdef ENVOY_ENABLE_YAML
        MessageUtil::jsonConvert(typed_struct.value(), validation_visitor, out_proto);
#else
        UNREFERENCED_PARAMETER(validation_visitor);
        IS_ENVOY_BUG("Attempting to use legacy JSON structs with JSON compiled out");
#endif
      }
    } // out_proto is expecting Struct, unpack directly
    else if (type != struct_type || out_proto.GetTypeName() == struct_type) {
      MessageUtil::unpackToOrThrow(typed_config, out_proto);
    } else {
#ifdef ENVOY_ENABLE_YAML
      ProtobufWkt::Struct struct_config;
      MessageUtil::unpackToOrThrow(typed_config, struct_config);
      MessageUtil::jsonConvert(struct_config, validation_visitor, out_proto);
#else
      IS_ENVOY_BUG("Attempting to use JSON structs with JSON compiled out");
#endif
    }
  }
}

absl::StatusOr<JitteredExponentialBackOffStrategyPtr>
Utility::buildJitteredExponentialBackOffStrategy(
    absl::optional<const envoy::config::core::v3::BackoffStrategy> backoff,
    Random::RandomGenerator& random, const uint32_t default_base_interval_ms,
    absl::optional<const uint32_t> default_max_interval_ms) {
  // BackoffStrategy config is specified
  if (backoff != absl::nullopt) {
    uint32_t base_interval_ms = PROTOBUF_GET_MS_REQUIRED(backoff.value(), base_interval);
    uint32_t max_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(backoff.value(), max_interval, base_interval_ms * 10);

    if (max_interval_ms < base_interval_ms) {
      return absl::InvalidArgumentError(
          "max_interval must be greater than or equal to the base_interval");
    }
    return std::make_unique<JitteredExponentialBackOffStrategy>(base_interval_ms, max_interval_ms,
                                                                random);
  }

  // default_base_interval_ms must be greater than zero
  if (default_base_interval_ms == 0) {
    return absl::InvalidArgumentError("default_base_interval_ms must be greater than zero");
  }

  // default maximum interval is specified
  if (default_max_interval_ms != absl::nullopt) {
    if (default_max_interval_ms.value() < default_base_interval_ms) {
      return absl::InvalidArgumentError(
          "default_max_interval_ms must be greater than or equal to the default_base_interval_ms");
    }
    return std::make_unique<JitteredExponentialBackOffStrategy>(
        default_base_interval_ms, default_max_interval_ms.value(), random);
  }
  // use default base interval
  return std::make_unique<JitteredExponentialBackOffStrategy>(
      default_base_interval_ms, default_base_interval_ms * 10, random);
}

absl::Status Utility::validateTerminalFilters(const std::string& name,
                                              const std::string& filter_type,
                                              const std::string& filter_chain_type,
                                              bool is_terminal_filter,
                                              bool last_filter_in_current_config) {
  if (is_terminal_filter && !last_filter_in_current_config) {
    return absl::InvalidArgumentError(
        fmt::format("Error: terminal filter named {} of type {} must be the "
                    "last filter in a {} filter chain.",
                    name, filter_type, filter_chain_type));
  } else if (!is_terminal_filter && last_filter_in_current_config) {
    absl::string_view extra = "";
    if (filter_chain_type == "router upstream http") {
      extra = " When upstream_http_filters are specified, they must explicitly end with an "
              "UpstreamCodec filter.";
    }
    return absl::InvalidArgumentError(fmt::format("Error: non-terminal filter named {} of type "
                                                  "{} is the last filter in a {} filter chain.{}",
                                                  name, filter_type, filter_chain_type, extra));
  }
  return absl::OkStatus();
}

} // namespace Config
} // namespace Envoy
