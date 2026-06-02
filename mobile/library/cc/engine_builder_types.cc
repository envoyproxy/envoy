#include "library/cc/engine_builder_types.h"

#include "absl/strings/str_cat.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"

namespace Envoy {
namespace Platform {

#ifdef ENVOY_MOBILE_XDS
XdsBuilder::XdsBuilder(std::string xds_server_address, const uint32_t xds_server_port)
    : xds_server_address_(std::move(xds_server_address)), xds_server_port_(xds_server_port) {}

XdsBuilder& XdsBuilder::addInitialStreamHeader(std::string header, std::string value) {
  envoy::config::core::v3::HeaderValue header_value;
  header_value.set_key(std::move(header));
  header_value.set_value(std::move(value));
  xds_initial_grpc_metadata_.emplace_back(std::move(header_value));
  return *this;
}

XdsBuilder& XdsBuilder::setSslRootCerts(std::string root_certs) {
  ssl_root_certs_ = std::move(root_certs);
  return *this;
}

XdsBuilder& XdsBuilder::addRuntimeDiscoveryService(std::string resource_name,
                                                   const int timeout_in_seconds) {
  rtds_resource_name_ = std::move(resource_name);
  rtds_timeout_in_seconds_ = timeout_in_seconds > 0 ? timeout_in_seconds : DefaultXdsTimeout;
  return *this;
}

XdsBuilder& XdsBuilder::addClusterDiscoveryService(std::string cds_resources_locator,
                                                   const int timeout_in_seconds) {
  enable_cds_ = true;
  cds_resources_locator_ = std::move(cds_resources_locator);
  cds_timeout_in_seconds_ = timeout_in_seconds > 0 ? timeout_in_seconds : DefaultXdsTimeout;
  return *this;
}

void XdsBuilder::build(envoy::config::bootstrap::v3::Bootstrap& bootstrap) const {
  auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
  ads_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
  ads_config->set_set_node_on_first_message_only(true);
  ads_config->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);

  auto& grpc_service = *ads_config->add_grpc_services();
  grpc_service.mutable_envoy_grpc()->set_cluster_name("base");
  grpc_service.mutable_envoy_grpc()->set_authority(
      absl::StrCat(xds_server_address_, ":", xds_server_port_));

  if (!xds_initial_grpc_metadata_.empty()) {
    grpc_service.mutable_initial_metadata()->Assign(xds_initial_grpc_metadata_.begin(),
                                                    xds_initial_grpc_metadata_.end());
  }

  if (!rtds_resource_name_.empty()) {
    auto* layered_runtime = bootstrap.mutable_layered_runtime();
    auto* layer = layered_runtime->add_layers();
    layer->set_name("rtds_layer");
    auto* rtds_layer = layer->mutable_rtds_layer();
    rtds_layer->set_name(rtds_resource_name_);
    auto* rtds_config = rtds_layer->mutable_rtds_config();
    rtds_config->mutable_ads();
    rtds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    rtds_config->mutable_initial_fetch_timeout()->set_seconds(rtds_timeout_in_seconds_);
  }

  if (enable_cds_) {
    auto* cds_config = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
    if (cds_resources_locator_.empty()) {
      cds_config->mutable_ads();
    } else {
      bootstrap.mutable_dynamic_resources()->set_cds_resources_locator(cds_resources_locator_);
      cds_config->mutable_api_config_source()->set_api_type(
          envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC);
      cds_config->mutable_api_config_source()->set_transport_api_version(
          envoy::config::core::v3::ApiVersion::V3);
    }
    cds_config->mutable_initial_fetch_timeout()->set_seconds(cds_timeout_in_seconds_);
    cds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    bootstrap.add_node_context_params("cluster");
    // Stat prefixes that we use in tests.
    auto* list =
        bootstrap.mutable_stats_config()->mutable_stats_matcher()->mutable_inclusion_list();
    list->add_patterns()->set_exact("cluster_manager.active_clusters");
    list->add_patterns()->set_exact("cluster_manager.cluster_added");
    list->add_patterns()->set_exact("cluster_manager.cluster_updated");
    list->add_patterns()->set_exact("cluster_manager.cluster_removed");
    // Allow SDS related stats.
    list->add_patterns()->mutable_safe_regex()->set_regex("sds\\..*");
    list->add_patterns()->mutable_safe_regex()->set_regex(".*\\.ssl_context_update_by_sds");
  }
}
#endif // ENVOY_MOBILE_XDS

} // namespace Platform
} // namespace Envoy
