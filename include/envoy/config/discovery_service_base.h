#pragma once

#include <string>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/cluster.pb.h"
#include "envoy/api/v2/endpoint.pb.h"
#include "envoy/api/v2/listener.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route_components.pb.h"
#include "envoy/api/v2/scoped_route.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/discovery_service_base.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

namespace Envoy {
namespace Config {
template <typename Prev, typename Current>
struct SubscriptionBase : public Config::SubscriptionCallbacks {
  static std::string getResourceName(envoy::config::core::v3::ApiVersion resource_api_version) {
    switch (resource_api_version) {
    case envoy::config::core::v3::ApiVersion::AUTO:
    case envoy::config::core::v3::ApiVersion::V2:
      return Prev().GetDescriptor()->full_name();
    case envoy::config::core::v3::ApiVersion::V3:
      return Current().GetDescriptor()->full_name();
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};
using RdsRouteConfigSubscriptionBase =
    SubscriptionBase<envoy::api::v2::RouteConfiguration,
                     envoy::config::route::v3::RouteConfiguration>;
using RtdsSubscriptionBase =
    SubscriptionBase<envoy::service::discovery::v2::Runtime, envoy::service::runtime::v3::Runtime>;
using ScopedRdsConfigSubscriptionBase =
    SubscriptionBase<envoy::api::v2::ScopedRouteConfiguration,
                     envoy::config::route::v3::ScopedRouteConfiguration>;
using VhdsSubscriptionBase =
    SubscriptionBase<envoy::api::v2::route::VirtualHost, envoy::service::runtime::v3::Runtime>;
using SdsApiBase = SubscriptionBase<envoy::api::v2::auth::Secret,
                                    envoy::extensions::transport_sockets::tls::v3::Secret>;
using CdsApiBase = SubscriptionBase<envoy::api::v2::Cluster, envoy::config::cluster::v3::Cluster>;
using EdsClusterBase = SubscriptionBase<envoy::api::v2::ClusterLoadAssignment,
                                        envoy::config::endpoint::v3::ClusterLoadAssignment>;
using LdsApiBase =
    SubscriptionBase<envoy::api::v2::Listener, envoy::config::listener::v3::Listener>;

} // namespace Config
} // namespace Envoy