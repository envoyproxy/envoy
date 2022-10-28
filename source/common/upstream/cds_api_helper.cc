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

std::vector<std::string>
CdsApiHelper::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                             const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                             const std::string& system_version_info) {
  Config::ScopedResume maybe_resume_eds_leds_sds;
  if (cm_.adsMux()) {
    // A cluster update pauses sending EDS and LEDS requests.
    std::vector<std::string> paused_xds_types{
        Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
        Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>()};
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.combine_sds_requests")) {
      paused_xds_types.push_back(
          Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>());
    }
    maybe_resume_eds_leds_sds = cm_.adsMux()->pause(paused_xds_types);
  }

  ENVOY_LOG(info, "{}: add {} cluster(s), remove {} cluster(s)", name_, added_resources.size(),
            removed_resources.size());

  std::vector<std::string> exception_msgs;
  absl::flat_hash_set<std::string> cluster_names(added_resources.size());
  bool any_applied = false;
  uint32_t added_or_updated = 0;
  uint32_t skipped = 0;
  for (const auto& resource : added_resources) {
    envoy::config::cluster::v3::Cluster cluster;
    TRY_ASSERT_MAIN_THREAD {
      cluster = dynamic_cast<const envoy::config::cluster::v3::Cluster&>(resource.get().resource());
      if (!cluster_names.insert(cluster.name()).second) {
        // NOTE: at this point, the first of these duplicates has already been successfully applied.
        throw EnvoyException(fmt::format("duplicate cluster {} found", cluster.name()));
      }
      if (cm_.addOrUpdateCluster(cluster, resource.get().version())) {
        any_applied = true;
        ENVOY_LOG(debug, "{}: add/update cluster '{}'", name_, cluster.name());
        ++added_or_updated;
      } else {
        ENVOY_LOG(debug, "{}: add/update cluster '{}' skipped", name_, cluster.name());
        ++skipped;
      }
    }
    END_TRY
    catch (const EnvoyException& e) {
      exception_msgs.push_back(fmt::format("{}: {}", cluster.name(), e.what()));
    }
  }
  for (const auto& resource_name : removed_resources) {
    if (cm_.removeCluster(resource_name)) {
      any_applied = true;
      ENVOY_LOG(debug, "{}: remove cluster '{}'", name_, resource_name);
    }
  }

  ENVOY_LOG(info, "{}: added/updated {} cluster(s), skipped {} unmodified cluster(s)", name_,
            added_or_updated, skipped);

  if (any_applied) {
    system_version_info_ = system_version_info;
  }
  return exception_msgs;
}

} // namespace Upstream
} // namespace Envoy
