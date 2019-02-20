#pragma once

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {

/**
 * Well-known cluster types, this supersede the service discovery types
 */
class ClusterTypeValues {
public:
  // Refer to the :ref:`static discovery type<arch_overview_service_discovery_types_static>`
  // for an explanation.
  const std::string Static = "envoy.cluster.static";

  // Refer to the :ref:`strict DNS discovery
  // type<arch_overview_service_discovery_types_strict_dns>`
  // for an explanation.
  const std::string StrictDns = "envoy.cluster.strict_dns";

  // Refer to the :ref:`logical DNS discovery
  // type<arch_overview_service_discovery_types_logical_dns>`
  // for an explanation.
  const std::string LogicalDns = "envoy.cluster.logical_dns";

  // Refer to the :ref:`service discovery type<arch_overview_service_discovery_types_eds>`
  // for an explanation.
  const std::string Eds = "envoy.cluster.eds";

  // Refer to the :ref:`original destination discovery
  // type<arch_overview_service_discovery_types_original_destination>`
  // for an explanation.
  const std::string OriginalDst = "envoy.cluster.original_dst";
};

typedef ConstSingleton<ClusterTypeValues> ClusterTypes;

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
