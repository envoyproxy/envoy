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
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/tag_producer_impl.h"

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

void Utility::translateApiConfigSource(
    const std::string& cluster, uint32_t refresh_delay_ms, const std::string& api_type,
    envoy::config::core::v3::ApiConfigSource& api_config_source) {
  // TODO(junr03): document the option to chose an api type once we have created
  // stronger constraints around v2.
  if (api_type == ApiType::get().Grpc) {
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    envoy::config::core::v3::GrpcService* grpc_service = api_config_source.add_grpc_services();
    grpc_service->mutable_envoy_grpc()->set_cluster_name(cluster);
  } else {
    ASSERT(api_type == ApiType::get().Rest);
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
    api_config_source.add_cluster_names(cluster);
  }

  api_config_source.mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(refresh_delay_ms));
}

Upstream::ClusterConstOptRef Utility::checkCluster(absl::string_view error_prefix,
                                                   absl::string_view cluster_name,
                                                   Upstream::ClusterManager& cm,
                                                   bool allow_added_via_api) {
  const auto cluster = cm.clusters().getCluster(cluster_name);
  if (!cluster.has_value()) {
    throw EnvoyException(fmt::format("{}: unknown cluster '{}'", error_prefix, cluster_name));
  }

  if (!allow_added_via_api && cluster->get().info()->addedViaApi()) {
    throw EnvoyException(fmt::format(
        "{}: invalid cluster '{}': currently only static (non-CDS) clusters are supported",
        error_prefix, cluster_name));
  }
  return cluster;
}

Upstream::ClusterConstOptRef
Utility::checkClusterAndLocalInfo(absl::string_view error_prefix, absl::string_view cluster_name,
                                  Upstream::ClusterManager& cm,
                                  const LocalInfo::LocalInfo& local_info) {
  checkLocalInfo(error_prefix, local_info);
  return checkCluster(error_prefix, cluster_name, cm);
}

void Utility::checkLocalInfo(absl::string_view error_prefix,
                             const LocalInfo::LocalInfo& local_info) {
  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    throw EnvoyException(
        fmt::format("{}: node 'id' and 'cluster' are required. Set it either in 'node' config or "
                    "via --service-node and --service-cluster options.",
                    error_prefix, local_info.node().DebugString()));
  }
}

void Utility::checkFilesystemSubscriptionBackingPath(const std::string& path, Api::Api& api) {
  // TODO(junr03): the file might be deleted between this check and the
  // watch addition.
  if (!api.fileSystem().fileExists(path)) {
    throw EnvoyException(fmt::format(
        "paths must refer to an existing path in the system: '{}' does not exist", path));
  }
}

void Utility::checkApiConfigSourceNames(
    const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  const bool is_grpc =
      (api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::GRPC ||
       api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);

  if (api_config_source.cluster_names().empty() && api_config_source.grpc_services().empty()) {
    throw EnvoyException(
        fmt::format("API configs must have either a gRPC service or a cluster name defined: {}",
                    api_config_source.DebugString()));
  }

  if (is_grpc) {
    if (!api_config_source.cluster_names().empty()) {
      throw EnvoyException(
          fmt::format("{}::(DELTA_)GRPC must not have a cluster name specified: {}",
                      api_config_source.GetTypeName(), api_config_source.DebugString()));
    }
    if (api_config_source.grpc_services().size() > 1) {
      throw EnvoyException(
          fmt::format("{}::(DELTA_)GRPC must have a single gRPC service specified: {}",
                      api_config_source.GetTypeName(), api_config_source.DebugString()));
    }
  } else {
    if (!api_config_source.grpc_services().empty()) {
      throw EnvoyException(
          fmt::format("{}, if not a gRPC type, must not have a gRPC service specified: {}",
                      api_config_source.GetTypeName(), api_config_source.DebugString()));
    }
    if (api_config_source.cluster_names().size() != 1) {
      throw EnvoyException(fmt::format("{} must have a singleton cluster name specified: {}",
                                       api_config_source.GetTypeName(),
                                       api_config_source.DebugString()));
    }
  }
}

void Utility::validateClusterName(const Upstream::ClusterManager::ClusterSet& primary_clusters,
                                  const std::string& cluster_name,
                                  const std::string& config_source) {
  const auto& it = primary_clusters.find(cluster_name);
  if (it == primary_clusters.end()) {
    throw EnvoyException(fmt::format("{} must have a statically defined non-EDS cluster: '{}' does "
                                     "not exist, was added via api, or is an EDS cluster",
                                     config_source, cluster_name));
  }
}

void Utility::checkApiConfigSourceSubscriptionBackingCluster(
    const Upstream::ClusterManager::ClusterSet& primary_clusters,
    const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  // We don't need to check backing sources for ADS sources, the backing cluster must be verified in
  // the ads_config.
  if (api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC ||
      api_config_source.api_type() ==
          envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC) {
    return;
  }
  Utility::checkApiConfigSourceNames(api_config_source);

  const bool is_grpc =
      (api_config_source.api_type() == envoy::config::core::v3::ApiConfigSource::GRPC);

  if (!api_config_source.cluster_names().empty()) {
    // All API configs of type REST and UNSUPPORTED_REST_LEGACY should have cluster names.
    // Additionally, some gRPC API configs might have a cluster name set instead
    // of an envoy gRPC.
    Utility::validateClusterName(primary_clusters, api_config_source.cluster_names()[0],
                                 api_config_source.GetTypeName());
  } else if (is_grpc) {
    // Some ApiConfigSources of type GRPC won't have a cluster name, such as if
    // they've been configured with google_grpc.
    if (api_config_source.grpc_services()[0].has_envoy_grpc()) {
      // If an Envoy gRPC exists, we take its cluster name.
      Utility::validateClusterName(primary_clusters,
                                   api_config_source.grpc_services()[0].envoy_grpc().cluster_name(),
                                   api_config_source.GetTypeName());
    }
  }
  // Otherwise, there is no cluster name to validate.
}

absl::optional<std::string>
Utility::getGrpcControlPlane(const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  if (api_config_source.grpc_services_size() > 0) {
    // Only checking for the first entry in grpc_services, because Envoy's xDS implementation
    // currently only considers the first gRPC endpoint and ignores any other xDS management servers
    // specified in an ApiConfigSource.
    if (api_config_source.grpc_services(0).has_envoy_grpc()) {
      return api_config_source.grpc_services(0).envoy_grpc().cluster_name();
    }
    if (api_config_source.grpc_services(0).has_google_grpc()) {
      return api_config_source.grpc_services(0).google_grpc().target_uri();
    }
  }
  return absl::nullopt;
}

std::chrono::milliseconds Utility::apiConfigSourceRefreshDelay(
    const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  if (!api_config_source.has_refresh_delay()) {
    throw EnvoyException("refresh_delay is required for REST API configuration sources");
  }

  return std::chrono::milliseconds(
      DurationUtil::durationToMilliseconds(api_config_source.refresh_delay()));
}

std::chrono::milliseconds Utility::apiConfigSourceRequestTimeout(
    const envoy::config::core::v3::ApiConfigSource& api_config_source) {
  return std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(api_config_source, request_timeout, 1000));
}

std::chrono::milliseconds Utility::configSourceInitialFetchTimeout(
    const envoy::config::core::v3::ConfigSource& config_source) {
  return std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(config_source, initial_fetch_timeout, 15000));
}

RateLimitSettings
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
  }
  return rate_limit_settings;
}

Stats::TagProducerPtr
Utility::createTagProducer(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                           const Stats::TagVector& cli_tags) {
  return std::make_unique<Stats::TagProducerImpl>(bootstrap.stats_config(), cli_tags);
}

Stats::StatsMatcherPtr
Utility::createStatsMatcher(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                            Stats::SymbolTable& symbol_table) {
  return std::make_unique<Stats::StatsMatcherImpl>(bootstrap.stats_config(), symbol_table);
}

Stats::HistogramSettingsConstPtr
Utility::createHistogramSettings(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  return std::make_unique<Stats::HistogramSettingsImpl>(bootstrap.stats_config());
}

Grpc::AsyncClientFactoryPtr Utility::factoryForGrpcApiConfigSource(
    Grpc::AsyncClientManager& async_client_manager,
    const envoy::config::core::v3::ApiConfigSource& api_config_source, Stats::Scope& scope,
    bool skip_cluster_check) {
  Utility::checkApiConfigSourceNames(api_config_source);

  if (api_config_source.api_type() != envoy::config::core::v3::ApiConfigSource::GRPC &&
      api_config_source.api_type() != envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
    throw EnvoyException(fmt::format("{} type must be gRPC: {}", api_config_source.GetTypeName(),
                                     api_config_source.DebugString()));
  }

  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.MergeFrom(api_config_source.grpc_services(0));

  return async_client_manager.factoryForGrpcService(grpc_service, scope, skip_cluster_check);
}

void Utility::translateOpaqueConfig(const ProtobufWkt::Any& typed_config,
                                    ProtobufMessage::ValidationVisitor& validation_visitor,
                                    Protobuf::Message& out_proto) {
  static const std::string struct_type =
      ProtobufWkt::Struct::default_instance().GetDescriptor()->full_name();
  static const std::string typed_struct_type =
      xds::type::v3::TypedStruct::default_instance().GetDescriptor()->full_name();
  static const std::string legacy_typed_struct_type =
      udpa::type::v1::TypedStruct::default_instance().GetDescriptor()->full_name();

  if (!typed_config.value().empty()) {
    // Unpack methods will only use the fully qualified type name after the last '/'.
    // https://github.com/protocolbuffers/protobuf/blob/3.6.x/src/google/protobuf/any.proto#L87
    absl::string_view type = TypeUtil::typeUrlToDescriptorFullName(typed_config.type_url());

    if (type == typed_struct_type) {
      xds::type::v3::TypedStruct typed_struct;
      MessageUtil::unpackTo(typed_config, typed_struct);
      // if out_proto is expecting Struct, return directly
      if (out_proto.GetDescriptor()->full_name() == struct_type) {
        out_proto.CopyFrom(typed_struct.value());
      } else {
        // The typed struct might match out_proto, or some earlier version, let
        // MessageUtil::jsonConvert sort this out.
        MessageUtil::jsonConvert(typed_struct.value(), validation_visitor, out_proto);
      }
    } else if (type == legacy_typed_struct_type) {
      udpa::type::v1::TypedStruct typed_struct;
      MessageUtil::unpackTo(typed_config, typed_struct);
      // if out_proto is expecting Struct, return directly
      if (out_proto.GetDescriptor()->full_name() == struct_type) {
        out_proto.CopyFrom(typed_struct.value());
      } else {
        // The typed struct might match out_proto, or some earlier version, let
        // MessageUtil::jsonConvert sort this out.
        MessageUtil::jsonConvert(typed_struct.value(), validation_visitor, out_proto);
      }
    } // out_proto is expecting Struct, unpack directly
    else if (type != struct_type || out_proto.GetDescriptor()->full_name() == struct_type) {
      MessageUtil::unpackTo(typed_config, out_proto);
    } else {
      ProtobufWkt::Struct struct_config;
      MessageUtil::unpackTo(typed_config, struct_config);
      MessageUtil::jsonConvert(struct_config, validation_visitor, out_proto);
    }
  }
}

} // namespace Config
} // namespace Envoy
