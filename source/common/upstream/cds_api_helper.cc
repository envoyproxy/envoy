#include "source/common/upstream/cds_api_helper.h"

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/grpc_mux.h"

#include "source/common/common/fmt.h"
#include "source/common/config/resource_name.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Upstream {

std::pair<uint32_t, std::vector<std::string>>
CdsApiHelper::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                             const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                             const std::string& system_version_info) {
  // A cluster update pauses sending EDS and LEDS requests.
  const std::vector<std::string> paused_xds_types{
      Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
      Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>(),
      Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>()};
  Config::ScopedResume resume_eds_leds_sds = xds_manager_.pause(paused_xds_types);

  ENVOY_LOG(
      info,
      "{}: response indicates {} added/updated cluster(s), {} removed cluster(s); applying changes",
      name_, added_resources.size(), removed_resources.size());

  std::vector<std::string> exception_msgs;
  absl::flat_hash_set<std::string> cluster_names(added_resources.size());
  bool any_applied = false;
  uint32_t added_or_updated = 0;
  uint32_t skipped = 0;
  for (const auto& resource : added_resources) {
    // Holds a reference to the name of the currently parsed cluster resource.
    // This is needed for the CATCH clause below.
    absl::string_view cluster_name = EMPTY_STRING;
    TRY_ASSERT_MAIN_THREAD {
      const envoy::config::cluster::v3::Cluster& cluster =
          dynamic_cast<const envoy::config::cluster::v3::Cluster&>(resource.get().resource());
      cluster_name = cluster.name();
      if (!cluster_names.insert(cluster.name()).second) {
        // NOTE: at this point, the first of these duplicates has already been successfully applied.
        exception_msgs.push_back(
            fmt::format("{}: duplicate cluster {} found", cluster_name, cluster_name));
        continue;
      }
      auto update_or_error = cm_.addOrUpdateCluster(cluster, resource.get().version());
      if (!update_or_error.status().ok()) {
        exception_msgs.push_back(
            fmt::format("{}: {}", cluster_name, update_or_error.status().message()));
        continue;
      }
      if (*update_or_error) {
        any_applied = true;
        ENVOY_LOG(debug, "{}: add/update cluster '{}'", name_, cluster_name);
        ++added_or_updated;
      } else {
        ENVOY_LOG(debug, "{}: add/update cluster '{}' skipped", name_, cluster_name);
        ++skipped;
      }
    }
    END_TRY
    CATCH(const EnvoyException& e,
          { exception_msgs.push_back(fmt::format("{}: {}", cluster_name, e.what())); });
  }

  uint32_t removed = 0;
  for (const auto& resource_name : removed_resources) {
    if (cm_.removeCluster(resource_name)) {
      any_applied = true;
      ENVOY_LOG(debug, "{}: remove cluster '{}'", name_, resource_name);
      ++removed;
    }
  }

  ENVOY_LOG(
      info,
      "{}: added/updated {} cluster(s) (skipped {} unmodified cluster(s)); removed {} cluster(s)",
      name_, added_or_updated, skipped, removed);

  if (any_applied) {
    system_version_info_ = system_version_info;
  }
  return std::pair{added_or_updated, exception_msgs};
}

} // namespace Upstream
} // namespace Envoy
