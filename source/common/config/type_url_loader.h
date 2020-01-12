#pragma once

#include <string>
#include <type_traits>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/cluster.pb.h"
#include "envoy/api/v2/endpoint.pb.h"
#include "envoy/api/v2/listener.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route_components.pb.h"
#include "envoy/api/v2/scoped_route.pb.h"
#include "envoy/config/cluster/v3alpha/cluster.pb.h"
#include "envoy/config/discovery_service_base.h"
#include "envoy/config/endpoint/v3alpha/endpoint.pb.h"
#include "envoy/config/listener/v3alpha/listener.pb.h"
#include "envoy/config/route/v3alpha/route.pb.h"
#include "envoy/config/route/v3alpha/route_components.pb.h"
#include "envoy/config/route/v3alpha/scoped_route.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3alpha/cert.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"
#include "envoy/service/runtime/v3alpha/rtds.pb.h"

namespace Envoy {
namespace Config {

template <typename DiscoveryServiceSubscription> std::string v2ResourceNameEmitter() {
  if (std::is_base_of<RdsRouteConfigSubscriptionBase, DiscoveryServiceSubscription>::value) {
    return envoy::api::v2::RouteConfiguration().GetDescriptor()->full_name();
  } else if (std::is_base_of<ScopedRdsConfigSubscriptionBase,
                             DiscoveryServiceSubscription>::value) {
    return envoy::api::v2::ScopedRouteConfiguration().GetDescriptor()->full_name();
  } else if (std::is_base_of<VhdsSubscriptionBase, DiscoveryServiceSubscription>::value) {
    return envoy::api::v2::route::VirtualHost().GetDescriptor()->full_name();
  } else if (std::is_base_of<RtdsSubscriptionBase, DiscoveryServiceSubscription>::value) {
    return envoy::service::discovery::v2::Runtime().GetDescriptor()->full_name();
  } else if (std::is_base_of<SdsApiBase, DiscoveryServiceSubscription>::value) {
    return envoy::api::v2::auth::Secret().GetDescriptor()->full_name();
  } else if (std::is_base_of<CdsApiBase, DiscoveryServiceSubscription>::value) {
    return envoy::api::v2::Cluster().GetDescriptor()->full_name();
  } else if (std::is_base_of<EdsClusterBase, DiscoveryServiceSubscription>::value) {
    return envoy::api::v2::ClusterLoadAssignment().GetDescriptor()->full_name();
  } else if (std::is_base_of<LdsApiBase, DiscoveryServiceSubscription>::value) {
    return envoy::api::v2::Listener().GetDescriptor()->full_name();
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

template <typename DiscoveryServiceSubscription> std::string v3alphaResourceNameEmitter() {
  if (std::is_base_of<RdsRouteConfigSubscriptionBase, DiscoveryServiceSubscription>::value) {
    return envoy::config::route::v3alpha::RouteConfiguration().GetDescriptor()->full_name();
  } else if (std::is_base_of<ScopedRdsConfigSubscriptionBase,
                             DiscoveryServiceSubscription>::value) {
    return envoy::config::route::v3alpha::ScopedRouteConfiguration().GetDescriptor()->full_name();
  } else if (std::is_base_of<VhdsSubscriptionBase, DiscoveryServiceSubscription>::value) {
    return envoy::config::route::v3alpha::VirtualHost().GetDescriptor()->full_name();
  } else if (std::is_base_of<RtdsSubscriptionBase, DiscoveryServiceSubscription>::value) {
    return envoy::service::runtime::v3alpha::Runtime().GetDescriptor()->full_name();
  } else if (std::is_base_of<SdsApiBase, DiscoveryServiceSubscription>::value) {
    return envoy::extensions::transport_sockets::tls::v3alpha::Secret()
        .GetDescriptor()
        ->full_name();
  } else if (std::is_base_of<CdsApiBase, DiscoveryServiceSubscription>::value) {
    return envoy::config::cluster::v3alpha::Cluster().GetDescriptor()->full_name();
  } else if (std::is_base_of<EdsClusterBase, DiscoveryServiceSubscription>::value) {
    return envoy::config::endpoint::v3alpha::ClusterLoadAssignment().GetDescriptor()->full_name();
  } else if (std::is_base_of<LdsApiBase, DiscoveryServiceSubscription>::value) {
    return envoy::config::listener::v3alpha::Listener().GetDescriptor()->full_name();
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

template <typename DiscoveryServiceSubscription>
std::string loadTypeUrl(envoy::config::core::v3alpha::ApiVersion resource_api_version) {
  switch (resource_api_version) {
  // automatically set api version as V2
  case envoy::config::core::v3alpha::ApiVersion::AUTO:
  case envoy::config::core::v3alpha::ApiVersion::V2:
    return v2ResourceNameEmitter<DiscoveryServiceSubscription>();
  case envoy::config::core::v3alpha::ApiVersion::V3ALPHA:
    return v3alphaResourceNameEmitter<DiscoveryServiceSubscription>();
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Config
} // namespace Envoy