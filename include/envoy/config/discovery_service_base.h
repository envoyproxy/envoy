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

#define RESOURCE_NAME_GETTER_REGISTRATION(PREV_RESOURCE, CURRENT_RESOURCE)                         \
  static std::string getResourceName(envoy::config::core::v3::ApiVersion resource_api_version) {   \
    switch (resource_api_version) {                                                                \
    case envoy::config::core::v3::ApiVersion::AUTO:                                                \
    case envoy::config::core::v3::ApiVersion::V2:                                                  \
      return PREV_RESOURCE;                                                                        \
    case envoy::config::core::v3::ApiVersion::V3:                                                  \
      return CURRENT_RESOURCE;                                                                     \
    default:                                                                                       \
      NOT_REACHED_GCOVR_EXCL_LINE;                                                                 \
    }                                                                                              \
  }

namespace Envoy {
namespace Config {
struct RdsRouteConfigSubscriptionBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::api::v2::RouteConfiguration().GetDescriptor()->full_name(),
      envoy::config::route::v3::RouteConfiguration().GetDescriptor()->full_name());
};
struct ScopedRdsConfigSubscriptionBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::api::v2::ScopedRouteConfiguration().GetDescriptor()->full_name(),
      envoy::config::route::v3::ScopedRouteConfiguration().GetDescriptor()->full_name());
};
struct VhdsSubscriptionBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::api::v2::route::VirtualHost().GetDescriptor()->full_name(),
      envoy::api::v2::route::VirtualHost().GetDescriptor()->full_name());
};
struct RtdsSubscriptionBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::service::discovery::v2::Runtime().GetDescriptor()->full_name(),
      envoy::service::runtime::v3::Runtime().GetDescriptor()->full_name());
};
struct SdsApiBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::api::v2::auth::Secret().GetDescriptor()->full_name(),
      envoy::extensions::transport_sockets::tls::v3::Secret().GetDescriptor()->full_name());
};
struct CdsApiBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::api::v2::Cluster().GetDescriptor()->full_name(),
      envoy::config::cluster::v3::Cluster().GetDescriptor()->full_name());
};
struct EdsClusterBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::api::v2::ClusterLoadAssignment().GetDescriptor()->full_name(),
      envoy::config::endpoint::v3::ClusterLoadAssignment().GetDescriptor()->full_name());
};
struct LdsApiBase : public Config::SubscriptionCallbacks {
  RESOURCE_NAME_GETTER_REGISTRATION(
      envoy::api::v2::Listener().GetDescriptor()->full_name(),
      envoy::config::listener::v3::Listener().GetDescriptor()->full_name());
};

} // namespace Config
} // namespace Envoy