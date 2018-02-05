#include "common/config/bootstrap_json.h"

#include "common/common/assert.h"
#include "common/config/address_json.h"
#include "common/config/cds_json.h"
#include "common/config/json_utility.h"
#include "common/config/lds_json.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

void BootstrapJson::translateClusterManagerBootstrap(
    const Json::Object& json_cluster_manager, envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  json_cluster_manager.validateSchema(Json::Schema::CLUSTER_MANAGER_SCHEMA);

  Optional<envoy::api::v2::core::ConfigSource> eds_config;
  if (json_cluster_manager.hasObject("sds")) {
    const auto json_sds = json_cluster_manager.getObject("sds");
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters()->Add();
    Config::CdsJson::translateCluster(*json_sds->getObject("cluster"),
                                      Optional<envoy::api::v2::core::ConfigSource>(), *cluster);
    Config::Utility::translateEdsConfig(
        *json_sds,
        *bootstrap.mutable_dynamic_resources()->mutable_deprecated_v1()->mutable_sds_config());
    eds_config.value(bootstrap.dynamic_resources().deprecated_v1().sds_config());
  }

  if (json_cluster_manager.hasObject("cds")) {
    const auto json_cds = json_cluster_manager.getObject("cds");
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters()->Add();
    Config::CdsJson::translateCluster(*json_cds->getObject("cluster"), eds_config, *cluster);
    Config::Utility::translateCdsConfig(
        *json_cds, *bootstrap.mutable_dynamic_resources()->mutable_cds_config());
  }

  for (const Json::ObjectSharedPtr& json_cluster :
       json_cluster_manager.getObjectArray("clusters")) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters()->Add();
    Config::CdsJson::translateCluster(*json_cluster, eds_config, *cluster);
  }

  auto* cluster_manager = bootstrap.mutable_cluster_manager();
  JSON_UTIL_SET_STRING(json_cluster_manager, *cluster_manager, local_cluster_name);
  if (json_cluster_manager.hasObject("outlier_detection")) {
    JSON_UTIL_SET_STRING(*json_cluster_manager.getObject("outlier_detection"),
                         *cluster_manager->mutable_outlier_detection(), event_log_path);
  }
}

void BootstrapJson::translateBootstrap(const Json::Object& json_config,
                                       envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  json_config.validateSchema(Json::Schema::TOP_LEVEL_CONFIG_SCHEMA);

  translateClusterManagerBootstrap(*json_config.getObject("cluster_manager"), bootstrap);

  if (json_config.hasObject("lds")) {
    auto* lds_config = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    Config::Utility::translateLdsConfig(*json_config.getObject("lds"), *lds_config);
  }

  for (const auto json_listener : json_config.getObjectArray("listeners")) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners()->Add();
    Config::LdsJson::translateListener(*json_listener, *listener);
  }

  JSON_UTIL_SET_STRING(json_config, bootstrap, flags_path);

  auto* stats_sinks = bootstrap.mutable_stats_sinks();
  if (json_config.hasObject("statsd_udp_ip_address")) {
    auto* stats_sink = stats_sinks->Add();
    stats_sink->set_name(Config::StatsSinkNames::get().STATSD);
    envoy::config::metrics::v2::StatsdSink statsd_sink;
    AddressJson::translateAddress(json_config.getString("statsd_udp_ip_address"), false, true,
                                  *statsd_sink.mutable_address());
    MessageUtil::jsonConvert(statsd_sink, *stats_sink->mutable_config());
  }

  if (json_config.hasObject("statsd_tcp_cluster_name")) {
    auto* stats_sink = stats_sinks->Add();
    stats_sink->set_name(Config::StatsSinkNames::get().STATSD);
    envoy::config::metrics::v2::StatsdSink statsd_sink;
    statsd_sink.set_tcp_cluster_name(json_config.getString("statsd_tcp_cluster_name"));
    MessageUtil::jsonConvert(statsd_sink, *stats_sink->mutable_config());
  }

  JSON_UTIL_SET_DURATION(json_config, bootstrap, stats_flush_interval);

  auto* watchdog = bootstrap.mutable_watchdog();
  JSON_UTIL_SET_DURATION(json_config, *watchdog, miss_timeout);
  JSON_UTIL_SET_DURATION(json_config, *watchdog, megamiss_timeout);
  JSON_UTIL_SET_DURATION(json_config, *watchdog, kill_timeout);
  JSON_UTIL_SET_DURATION(json_config, *watchdog, multikill_timeout);

  const auto http = json_config.getObject("tracing", true)->getObject("http", true);
  if (http->hasObject("driver")) {
    const auto driver = http->getObject("driver");
    auto* http_tracing = bootstrap.mutable_tracing()->mutable_http();
    http_tracing->set_name("envoy." + driver->getString("type"));
    MessageUtil::loadFromJson(driver->getObject("config")->asJsonString(),
                              *http_tracing->mutable_config());
  }

  if (json_config.hasObject("rate_limit_service")) {
    const auto json_rate_limit_service = json_config.getObject("rate_limit_service");
    auto* rate_limit_service = bootstrap.mutable_rate_limit_service();
    ASSERT(json_rate_limit_service->getString("type") == "grpc_service");
    JSON_UTIL_SET_STRING(*json_rate_limit_service->getObject("config"), *rate_limit_service,
                         cluster_name);
  }

  const auto json_admin = json_config.getObject("admin");
  auto* admin = bootstrap.mutable_admin();
  JSON_UTIL_SET_STRING(*json_admin, *admin, access_log_path);
  JSON_UTIL_SET_STRING(*json_admin, *admin, profile_path);
  AddressJson::translateAddress(json_admin->getString("address"), true, true,
                                *admin->mutable_address());

  if (json_config.hasObject("runtime")) {
    const auto json_runtime = json_config.getObject("runtime");
    auto* runtime = bootstrap.mutable_runtime();
    JSON_UTIL_SET_STRING(*json_runtime, *runtime, symlink_root);
    JSON_UTIL_SET_STRING(*json_runtime, *runtime, subdirectory);
    JSON_UTIL_SET_STRING(*json_runtime, *runtime, override_subdirectory);
  }
}

} // namespace Config
} // namespace Envoy
