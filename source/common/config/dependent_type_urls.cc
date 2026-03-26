#include "source/common/config/dependent_type_urls.h"

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"

#include "source/common/config/resource_name.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Config {

const std::vector<std::string>& dependentTypeUrls(absl::string_view type_url) {
  static const absl::flat_hash_map<std::string, std::vector<std::string>> dependent_type_urls = {
      {Envoy::Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(),
       {Envoy::Config::getTypeUrl<envoy::config::route::v3::VirtualHost>()}},
      {Envoy::Config::getTypeUrl<envoy::config::route::v3::ScopedRouteConfiguration>(),
       {Envoy::Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>()}},
      {Envoy::Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
       {Envoy::Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>()}},
      {Envoy::Config::getTypeUrl<envoy::config::listener::v3::Listener>(),
       {Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(),
        Config::getTypeUrl<envoy::config::route::v3::ScopedRouteConfiguration>(),
        Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>()}},
      {Envoy::Config::getTypeUrl<envoy::config::cluster::v3::Cluster>(),
       {Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
        Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>(),
        Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>()}},
  };
  static const std::vector<std::string> empty;

  const auto it = dependent_type_urls.find(type_url);
  if (it == dependent_type_urls.end()) {
    return empty;
  }

  return it->second;
}

} // namespace Config
} // namespace Envoy
