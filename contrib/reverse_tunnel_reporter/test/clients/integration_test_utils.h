#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"

#include "source/common/common/fmt.h"

#include "test/config/utility.h"

#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters/event_reporter.pb.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

constexpr absl::string_view downstreamExtension =
    "envoy.bootstrap.reverse_tunnel.downstream_socket_interface";
constexpr absl::string_view upstreamExtension =
    "envoy.bootstrap.reverse_tunnel.upstream_socket_interface";
constexpr absl::string_view downstreamCluster = "downstreamCluster";
constexpr absl::string_view upstreamCluster = "upstreamCluster";
constexpr absl::string_view upstreamListener = "upstreamListener";
constexpr absl::string_view upstreamFilter = "envoy.filters.network.reverse_tunnel";
constexpr absl::string_view downstreamTenant = "downstreamTenant";
constexpr absl::string_view downstreamResolver = "envoy.resolvers.reverse_connection";

constexpr int upstreamPort = 9000;
constexpr int reportingPort = 8082;

using envoy::config::bootstrap::v3::Bootstrap;
using envoy::config::cluster::v3::Cluster;
using envoy::config::core::v3::TypedExtensionConfig;
using envoy::config::listener::v3::Listener;
using envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
    DownstreamReverseConnectionSocketInterface;
using envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
    UpstreamReverseConnectionSocketInterface;
using envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel;
using envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::EventReporterConfig;

inline TypedExtensionConfig getDownstreamExtension() {
  DownstreamReverseConnectionSocketInterface cfg;
  cfg.set_stat_prefix(downstreamExtension);
  cfg.set_enable_detailed_stats(true);

  TypedExtensionConfig ext;
  ext.set_name(downstreamExtension);
  ext.mutable_typed_config()->PackFrom(cfg);

  return ext;
}

inline TypedExtensionConfig getUpstreamExtension(const EventReporterConfig& config,
                                                 bool enable_tenant_isolation) {
  UpstreamReverseConnectionSocketInterface cfg;
  cfg.set_stat_prefix(upstreamExtension);
  cfg.set_enable_detailed_stats(true);
  cfg.mutable_enable_tenant_isolation()->set_value(enable_tenant_isolation);
  const std::string reporterName =
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter";
  auto* reporter = cfg.mutable_reporter_config();
  reporter->set_name(reporterName);
  reporter->mutable_typed_config()->PackFrom(config);

  TypedExtensionConfig ext;
  ext.set_name(upstreamExtension);
  ext.mutable_typed_config()->PackFrom(cfg);

  return ext;
}

inline Cluster getDownstreamCluster(absl::string_view localhost) {
  Cluster cluster;
  cluster.set_name(downstreamCluster);
  cluster.set_type(Cluster::STATIC);
  cluster.mutable_connect_timeout()->set_seconds(30);
  cluster.mutable_load_assignment()->set_cluster_name(downstreamCluster);

  auto* sa = cluster.mutable_load_assignment()
                 ->mutable_endpoints()
                 ->Add()
                 ->add_lb_endpoints()
                 ->mutable_endpoint()
                 ->mutable_address()
                 ->mutable_socket_address();

  sa->set_address(localhost);
  sa->set_port_value(upstreamPort);

  return cluster;
}

inline Cluster getUpstreamCluster(absl::string_view localhost) {
  Cluster cluster;
  cluster.set_name(upstreamCluster);
  cluster.set_type(Cluster::STATIC);
  cluster.mutable_connect_timeout()->set_seconds(30);
  cluster.mutable_load_assignment()->set_cluster_name(upstreamCluster);

  auto* sa = cluster.mutable_load_assignment()
                 ->mutable_endpoints()
                 ->Add()
                 ->add_lb_endpoints()
                 ->mutable_endpoint()
                 ->mutable_address()
                 ->mutable_socket_address();

  sa->set_address(localhost);
  sa->set_port_value(reportingPort);

  return cluster;
}

inline Listener getUpstreamListener(absl::string_view anyhost) {
  Listener listener;
  listener.set_name(upstreamListener);
  listener.set_stat_prefix(upstreamListener);

  auto* sa = listener.mutable_address()->mutable_socket_address();
  sa->set_address(anyhost);
  sa->set_port_value(upstreamPort);

  auto* filter = listener.add_filter_chains()->add_filters();
  filter->set_name(upstreamFilter);

  ReverseTunnel rtFilter;
  rtFilter.mutable_ping_interval()->set_seconds(300); // No ping timeouts.

  filter->mutable_typed_config()->PackFrom(rtFilter);

  return listener;
}

inline Listener getDownstreamListener(const std::string& name, int num_listeners) {
  Listener listener;
  listener.set_name(name);

  auto* sa = listener.mutable_address()->mutable_socket_address();
  sa->set_address(fmt::format("rc://{}:{}:{}@{}:{}", name, downstreamCluster, downstreamTenant,
                              downstreamCluster, num_listeners));
  sa->set_port_value(0);
  sa->set_resolver_name(downstreamResolver);

  listener.mutable_listener_filters_timeout()->set_seconds(0);

  auto* filter_chain = listener.add_filter_chains();
  auto* filter = filter_chain->add_filters();
  filter->set_name("envoy.filters.network.http_connection_manager");

  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
  hcm.set_stat_prefix(name);

  auto* route_config = hcm.mutable_route_config();
  auto* vh = route_config->add_virtual_hosts();
  vh->set_name(name);
  vh->add_domains("*");

  auto* route = vh->add_routes();
  route->mutable_match()->set_prefix("/");
  auto* direct_response = route->mutable_direct_response();
  direct_response->set_status(200);
  direct_response->mutable_body()->set_inline_string("reverse connection listener OK");

  auto* http_filter = hcm.add_http_filters();
  http_filter->set_name("envoy.filters.http.router");
  envoy::extensions::filters::http::router::v3::Router router_cfg;
  http_filter->mutable_typed_config()->PackFrom(router_cfg);

  filter->mutable_typed_config()->PackFrom(hcm);

  return listener;
}

void addCluster(Cluster&& cluster, ConfigHelper& config_helper) {
  config_helper.addConfigModifier([cluster = std::move(cluster)](Bootstrap& bootstrap) {
    *(bootstrap.mutable_static_resources()->mutable_clusters()->Add()) = cluster;
  });
}

void addListener(Listener&& listener, ConfigHelper& config_helper, int& current) {
  config_helper.addConfigModifier([listener = std::move(listener)](Bootstrap& bootstrap) {
    *(bootstrap.mutable_static_resources()->mutable_listeners()->Add()) = listener;
  });

  ++current;
}

void addBootstrapExtension(TypedExtensionConfig&& extension, ConfigHelper& config_helper) {
  config_helper.addConfigModifier([extension = std::move(extension)](Bootstrap& bootstrap) {
    *(bootstrap.mutable_bootstrap_extensions()->Add()) = extension;
  });
}

void removeListener(const std::string& name, ConfigHelper& config_helper, int& current) {
  config_helper.addConfigModifier([&name](Bootstrap& bootstrap) {
    auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();

    for (int i = 0; i < listeners->size(); ++i) {
      if (listeners->Get(i).name() == name) {
        listeners->DeleteSubrange(i, 1);
        break;
      }
    }
  });

  --current;
}

Cluster getHttp2Cluster(Cluster& cluster) {
  ConfigHelper::HttpProtocolOptions http2_options;
  http2_options.mutable_explicit_http_config()->mutable_http2_protocol_options();

  (*cluster.mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(http2_options);

  return cluster;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
