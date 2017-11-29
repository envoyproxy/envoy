#include "common/config/utility.h"

#include <unordered_set>

#include "common/common/assert.h"
#include "common/common/hex.h"
#include "common/common/utility.h"
#include "common/config/json_utility.h"
#include "common/config/resources.h"
#include "common/config/well_known_names.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/stats/stats_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {

void Utility::translateApiConfigSource(const std::string& cluster, uint32_t refresh_delay_ms,
                                       const std::string& api_type,
                                       envoy::api::v2::ApiConfigSource& api_config_source) {
  // TODO(junr03): document the option to chose an api type once we have created
  // stronger constraints around v2.
  if (api_type == ApiType::get().RestLegacy) {
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::REST_LEGACY);
  } else if (api_type == ApiType::get().Rest) {
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::REST);
  } else {
    ASSERT(api_type == ApiType::get().Grpc);
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
  }
  api_config_source.add_cluster_name(cluster);
  api_config_source.mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(refresh_delay_ms));
}

void Utility::checkCluster(const std::string& error_prefix, const std::string& cluster_name,
                           Upstream::ClusterManager& cm) {
  Upstream::ThreadLocalCluster* cluster = cm.get(cluster_name);
  if (cluster == nullptr) {
    throw EnvoyException(fmt::format("{}: unknown cluster '{}'", error_prefix, cluster_name));
  }

  if (cluster->info()->addedViaApi()) {
    throw EnvoyException(fmt::format("{}: invalid cluster '{}': currently only "
                                     "static (non-CDS) clusters are supported",
                                     error_prefix, cluster_name));
  }
}

void Utility::checkClusterAndLocalInfo(const std::string& error_prefix,
                                       const std::string& cluster_name,
                                       Upstream::ClusterManager& cm,
                                       const LocalInfo::LocalInfo& local_info) {
  checkCluster(error_prefix, cluster_name, cm);
  checkLocalInfo(error_prefix, local_info);
}

void Utility::checkLocalInfo(const std::string& error_prefix,
                             const LocalInfo::LocalInfo& local_info) {
  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    throw EnvoyException(
        fmt::format("{}: setting --service-cluster and --service-node is required", error_prefix));
  }
}

std::chrono::milliseconds
Utility::apiConfigSourceRefreshDelay(const envoy::api::v2::ApiConfigSource& api_config_source) {
  return std::chrono::milliseconds(
      Protobuf::util::TimeUtil::DurationToMilliseconds(api_config_source.refresh_delay()));
}

void Utility::translateEdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::ConfigSource& eds_config) {
  translateApiConfigSource(json_config.getObject("cluster")->getString("name"),
                           json_config.getInteger("refresh_delay_ms", 30000),
                           json_config.getString("api_type", ApiType::get().RestLegacy),
                           *eds_config.mutable_api_config_source());
}

void Utility::translateCdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::ConfigSource& cds_config) {
  translateApiConfigSource(json_config.getObject("cluster")->getString("name"),
                           json_config.getInteger("refresh_delay_ms", 30000),
                           json_config.getString("api_type", ApiType::get().RestLegacy),
                           *cds_config.mutable_api_config_source());
}

void Utility::translateRdsConfig(const Json::Object& json_rds,
                                 envoy::api::v2::filter::network::Rds& rds) {
  json_rds.validateSchema(Json::Schema::RDS_CONFIGURATION_SCHEMA);

  const std::string name = json_rds.getString("route_config_name", "");
  checkObjNameLength("Invalid route_config name", name);
  rds.set_route_config_name(name);

  translateApiConfigSource(json_rds.getString("cluster"),
                           json_rds.getInteger("refresh_delay_ms", 30000),
                           json_rds.getString("api_type", ApiType::get().RestLegacy),
                           *rds.mutable_config_source()->mutable_api_config_source());
}

void Utility::translateLdsConfig(const Json::Object& json_lds,
                                 envoy::api::v2::ConfigSource& lds_config) {
  json_lds.validateSchema(Json::Schema::LDS_CONFIG_SCHEMA);
  translateApiConfigSource(json_lds.getString("cluster"),
                           json_lds.getInteger("refresh_delay_ms", 30000),
                           json_lds.getString("api_type", ApiType::get().RestLegacy),
                           *lds_config.mutable_api_config_source());
}

std::string Utility::resourceName(const ProtobufWkt::Any& resource) {
  if (resource.type_url() == Config::TypeUrl::get().Listener) {
    return MessageUtil::anyConvert<envoy::api::v2::Listener>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().RouteConfiguration) {
    return MessageUtil::anyConvert<envoy::api::v2::RouteConfiguration>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().Cluster) {
    return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().ClusterLoadAssignment) {
    return MessageUtil::anyConvert<envoy::api::v2::ClusterLoadAssignment>(resource).cluster_name();
  }
  throw EnvoyException(
      fmt::format("Unknown type URL {} in DiscoveryResponse", resource.type_url()));
}

std::vector<Stats::TagExtractorPtr>
Utility::createTagExtractors(const envoy::api::v2::Bootstrap& bootstrap) {
  std::vector<Stats::TagExtractorPtr> tag_extractors;

  // Ensure no tag names are repeated.
  std::unordered_set<std::string> names;
  auto add_tag = [&names, &tag_extractors](const std::string& name, const std::string& regex) {
    if (!names.emplace(name).second) {
      throw EnvoyException(fmt::format("Tag name '{}' specified twice.", name));
    }

    tag_extractors.emplace_back(Stats::TagExtractorImpl::createTagExtractor(name, regex));
  };

  // Add defaults.
  if (!bootstrap.stats_config().has_use_all_default_tags() ||
      bootstrap.stats_config().use_all_default_tags().value()) {
    for (const std::pair<std::string, std::string>& default_tag :
         TagNames::get().name_regex_pairs_) {
      add_tag(default_tag.first, default_tag.second);
    }
  }

  // Add custom tags.
  for (const envoy::api::v2::TagSpecifier& tag_specifier : bootstrap.stats_config().stats_tags()) {
    add_tag(tag_specifier.tag_name(), tag_specifier.regex());
  }

  return tag_extractors;
}

void Utility::checkObjNameLength(const std::string& error_prefix, const std::string& name) {
  if (name.length() > Stats::RawStatData::maxObjNameLength()) {
    throw EnvoyException(fmt::format("{}: Length of {} ({}) exceeds allowed maximum length ({})",
                                     error_prefix, name, name.length(),
                                     Stats::RawStatData::maxObjNameLength()));
  }
}

} // namespace Config
} // namespace Envoy
