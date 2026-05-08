#include "test/cc/integration/base/test_engine_and_server.h"

#include <memory>
#include <string>
#include <utility>

#include "test/cc/integration/base/test_engine_builder.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/strings/str_cat.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "test/common/integration/test_server.h"
#include "source/common/common/assert.h"

namespace Envoy {

TestEngineAndServer::TestEngineAndServer(
    Platform::TestEngineBuilder& engine_builder, TestServerType type,
    const absl::flat_hash_map<std::string, std::string>& headers, absl::string_view body,
    const absl::flat_hash_map<std::string, std::string>& trailers) {
  test_server_.start(type);
  test_server_.setResponse(headers, body, trailers);

  // Set up route configuration and cluster for ClientEngineBuilder to route
  // requests to test_server_.
  ::envoy::config::route::v3::RouteConfiguration route_configuration;
  route_configuration.set_name("route_config");
  auto* virtual_host = route_configuration.add_virtual_hosts();
  virtual_host->set_name("test_virtual_host");
  virtual_host->add_domains("*");
  auto* route = virtual_host->add_routes();
  route->mutable_match()->set_prefix("/");
  route->mutable_route()->set_cluster("test_cluster");
  engine_builder.setHcmRouteConfiguration(std::move(route_configuration));

  ::envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("test_cluster");
  cluster.mutable_connect_timeout()->set_seconds(5);
  auto* endpoint =
      cluster.mutable_load_assignment()->add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  endpoint->mutable_address()->mutable_socket_address()->set_address(test_server_.getIpAddress());
  endpoint->mutable_address()->mutable_socket_address()->set_port_value(test_server_.getPort());
  cluster.mutable_load_assignment()->set_cluster_name("test_cluster");
  cluster.set_type(::envoy::config::cluster::v3::Cluster::STATIC);

  bool use_tls = type == TestServerType::HTTP1_WITH_TLS || type == TestServerType::HTTP2_WITH_TLS ||
                 type == TestServerType::HTTP3;

  ::envoy::extensions::upstreams::http::v3::HttpProtocolOptions protocol_options;

  if (use_tls) {
    ::envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    tls_context.mutable_common_tls_context()
        ->mutable_validation_context()
        ->set_trust_chain_verification(::envoy::extensions::transport_sockets::tls::v3::
                                           CertificateValidationContext::ACCEPT_UNTRUSTED);
    tls_context.set_sni("www.lyft.com");

    if (type == TestServerType::HTTP2_WITH_TLS) {
      tls_context.mutable_common_tls_context()->add_alpn_protocols("h2");
      protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    } else if (type == TestServerType::HTTP1_WITH_TLS) {
      tls_context.mutable_common_tls_context()->add_alpn_protocols("http/1.1");
      protocol_options.mutable_explicit_http_config()
          ->mutable_http_protocol_options()
          ->set_enable_trailers(true);
    }
    cluster.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
    cluster.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);

    if (type == TestServerType::HTTP3) {
      protocol_options.mutable_explicit_http_config()->mutable_http3_protocol_options();
      ::envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport h3_inner_socket;
      h3_inner_socket.mutable_upstream_tls_context()->CopyFrom(tls_context);
      ::envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
          h3_proxy_socket;
      h3_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(h3_inner_socket);
      h3_proxy_socket.mutable_transport_socket()->set_name("envoy.transport_sockets.quic");
      cluster.mutable_transport_socket()->set_name("envoy.transport_sockets.http_11_proxy");
      cluster.mutable_transport_socket()->mutable_typed_config()->PackFrom(h3_proxy_socket);
    }
  } else {
    cluster.mutable_transport_socket()->set_name("envoy.transport_sockets.raw_buffer");
    ::envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
    cluster.mutable_transport_socket()->mutable_typed_config()->PackFrom(raw_buffer);
    protocol_options.mutable_explicit_http_config()
        ->mutable_http_protocol_options()
        ->set_enable_trailers(true);
  }

  (*cluster.mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(protocol_options);

  engine_builder.addCluster(std::move(cluster));

  auto engine = engine_builder.build();
  RELEASE_ASSERT(engine.ok(), absl::StrCat("Failed to build engine: ", engine.status().message()));
  engine_ = *std::move(engine);
}

TestEngineAndServer::~TestEngineAndServer() {
  test_server_.shutdown();
  if (engine_ != nullptr) {
    engine_->terminate();
  }
}

std::shared_ptr<Platform::Engine> TestEngineAndServer::engine() { return engine_; }

TestServer& TestEngineAndServer::test_server() { return test_server_; }

} // namespace Envoy
