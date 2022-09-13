#include "envoy/api/v2/endpoint.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/config/metadata.h"
#include "source/common/http/exception.h"
#include "source/common/protobuf/protobuf.h"

#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

std::string ipSuppressEnvoyHeadersTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return fmt::format(
      "{}_{}",
      TestUtility::ipTestParamsToString(
          ::testing::TestParamInfo<Network::Address::IpVersion>(std::get<0>(params.param), 0)),
      std::get<1>(params.param) ? "with_x_envoy_from_router" : "without_x_envoy_from_router");
}

void disableHeaderValueOptionAppend(
    Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& header_value_options) {
  for (auto& i : header_value_options) {
    i.set_append_action(envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  }
}

const std::string http_connection_mgr_config = R"EOF(
http_filters:
  - name: envoy.filters.http.router
codec_type: HTTP1
use_remote_address: false
xff_num_trusted_hops: 1
stat_prefix: header_test
route_config:
  virtual_hosts:
    - name: no-headers
      domains: ["no-headers.com"]
      routes:
        - match: { prefix: "/" }
          route: { cluster: "cluster_0" }
    - name: vhost-headers
      domains: ["vhost-headers.com"]
      request_headers_to_add:
        - header:
            key: "x-vhost-request"
            value: "vhost"
      request_headers_to_remove: ["x-vhost-request-remove"]
      response_headers_to_add:
        - header:
            key: "x-vhost-response"
            value: "vhost"
      response_headers_to_remove: ["x-vhost-response-remove"]
      routes:
        - match: { prefix: "/vhost-only" }
          route: { cluster: "cluster_0" }
        - match: { prefix: "/vhost-and-route" }
          request_headers_to_add:
            - header:
                key: "x-route-request"
                value: "route"
          request_headers_to_remove: ["x-route-request-remove"]
          response_headers_to_add:
            - header:
                key: "x-route-response"
                value: "route"
          response_headers_to_remove: ["x-route-response-remove"]
          route:
            cluster: cluster_0
        - match: { prefix: "/vhost-route-and-weighted-clusters" }
          request_headers_to_add:
            - header:
                key: "x-route-request"
                value: "route"
          request_headers_to_remove: ["x-route-request-remove"]
          response_headers_to_add:
            - header:
                key: "x-route-response"
                value: "route"
          response_headers_to_remove: ["x-route-response-remove"]
          route:
            weighted_clusters:
              clusters:
                - name: cluster_0
                  weight: 100
                  request_headers_to_add:
                    - header:
                        key: "x-weighted-cluster-request"
                        value: "weighted-cluster-1"
                  request_headers_to_remove: ["x-weighted-cluster-request-remove"]
                  response_headers_to_add:
                    - header:
                        key: "x-weighted-cluster-response"
                        value: "weighted-cluster-1"
                  response_headers_to_remove: ["x-weighted-cluster-response-remove"]
    - name: route-headers
      domains: ["route-headers.com"]
      routes:
        - match: { prefix: "/route-only" }
          request_headers_to_add:
            - header:
                key: "x-route-request"
                value: "route"
          request_headers_to_remove: ["x-route-request-remove"]
          response_headers_to_add:
            - header:
                key: "x-route-response"
                value: "route"
            - header:
                key: "details"
                value: "%RESPONSE_CODE_DETAILS%"
          response_headers_to_remove: ["x-route-response-remove"]
          route:
            cluster: cluster_0
    - name: xff-headers
      domains: ["xff-headers.com"]
      routes:
        - match: { prefix: "/test" }
          route:
            cluster: cluster_0
          request_headers_to_add:
            - header:
                key: "x-real-ip"
                value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    - name: append-same-headers
      domains: ["append-same-headers.com"]
      request_headers_to_add:
        - header:
            key: "x-foo"
            value: "value1"
        - header:
            key: "user-agent"
            value: "token1"
      routes:
        - match: { prefix: "/test" }
          route:
            cluster: cluster_0
          request_headers_to_add:
            - header:
                key: "x-foo"
                value: "value2"
            - header:
                key: "user-agent"
                value: "token2"
    - name: path-sanitization
      domains: ["path-sanitization.com"]
      routes:
        - match: { prefix: "/private" }
          route:
            cluster: cluster_0
          request_headers_to_add:
            - header:
                key: "x-site"
                value: "private"
        - match: { prefix: "/public" }
          route:
            cluster: cluster_0
          request_headers_to_add:
            - header:
                key: "x-site"
                value: "public"
)EOF";

} // namespace

class HeaderIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest {
public:
  HeaderIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {}

  bool routerSuppressEnvoyHeaders() const { return std::get<1>(GetParam()); }

  enum HeaderMode {
    Append = 1,
    Replace = 2,
  };

  void TearDown() override {
    if (eds_connection_ != nullptr) {
      AssertionResult result = eds_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = eds_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      eds_connection_.reset();
    }
  }

  void addHeader(Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>* field,
                 const std::string& key, const std::string& value, bool append) {
    envoy::config::core::v3::HeaderValueOption* header_value_option = field->Add();
    auto* mutable_header = header_value_option->mutable_header();
    mutable_header->set_key(key);
    mutable_header->set_value(value);
    header_value_option->set_append_action(
        append ? envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD
               : envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  }

  void prepareEDS() {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      ASSERT(static_resources->clusters_size() == 1);

      static_resources->mutable_clusters(0)->CopyFrom(
          TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(
              R"EOF(
                  name: cluster_0
                  type: EDS
                  eds_cluster_config:
                    eds_config:
                      resource_api_version: V3
                      api_config_source:
                        api_type: GRPC
                        transport_api_version: V3
                        grpc_services:
                          envoy_grpc:
                            cluster_name: "eds-cluster"
              )EOF"));

      // TODO(zuercher): Make ConfigHelper EDS-aware and get rid of this hack:
      // ConfigHelper expects the number of ports assigned to upstreams to match the number of
      // static hosts assigned ports. So give it a place to put the port for our EDS host. This
      // host must come before the eds-cluster's host to keep the upstreams and ports in the same
      // order.
      static_resources->add_clusters()->CopyFrom(
          TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(
              R"EOF(
                      name: unused-cluster
                      type: STATIC
                      lb_policy: ROUND_ROBIN
                      load_assignment:
                        cluster_name: unused-cluster
                        endpoints:
                        - lb_endpoints:
                          - endpoint:
                              address:
                                socket_address:
                                  address: {}
                                  port_value: 0
                  )EOF",
              Network::Test::getLoopbackAddressString(version_))));

      static_resources->add_clusters()->CopyFrom(
          TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(
              R"EOF(
                      name: eds-cluster
                      type: STATIC
                      lb_policy: ROUND_ROBIN
                      typed_extension_protocol_options:
                        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
                          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
                          explicit_http_config:
                            http2_protocol_options: {{}}
                      connect_timeout: 5s
                      load_assignment:
                        cluster_name: eds-cluster
                        endpoints:
                        - lb_endpoints:
                          - endpoint:
                              address:
                                socket_address:
                                  address: {}
                                  port_value: 0
                  )EOF",
              Network::Test::getLoopbackAddressString(version_))));
    });

    use_eds_ = true;
  }

  void initializeFilter(HeaderMode mode, bool inject_route_config_headers) {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          // Overwrite default config with our own.
          TestUtility::loadFromYaml(http_connection_mgr_config, hcm);
          envoy::extensions::filters::http::router::v3::Router router_config;
          router_config.set_suppress_envoy_headers(routerSuppressEnvoyHeaders());
          hcm.mutable_http_filters(0)->mutable_typed_config()->PackFrom(router_config);

          const bool append = mode == HeaderMode::Append;

          auto* route_config = hcm.mutable_route_config();
          if (inject_route_config_headers) {
            // Configure route config level headers.
            addHeader(route_config->mutable_response_headers_to_add(), "x-routeconfig-response",
                      "routeconfig", append);
            route_config->add_response_headers_to_remove("x-routeconfig-response-remove");
            addHeader(route_config->mutable_request_headers_to_add(), "x-routeconfig-request",
                      "routeconfig", append);
            route_config->add_request_headers_to_remove("x-routeconfig-request-remove");
          }

          if (use_eds_) {
            addHeader(route_config->mutable_response_headers_to_add(), "x-routeconfig-dynamic",
                      R"(%UPSTREAM_METADATA(["test.namespace", "key"])%)", append);

            // Iterate over VirtualHosts, nested Routes and WeightedClusters, adding a dynamic
            // response header.
            for (auto& vhost : *route_config->mutable_virtual_hosts()) {
              addHeader(vhost.mutable_response_headers_to_add(), "x-vhost-dynamic",
                        R"(vhost:%UPSTREAM_METADATA(["test.namespace", "key"])%)", append);

              for (auto& route : *vhost.mutable_routes()) {
                addHeader(route.mutable_response_headers_to_add(), "x-route-dynamic",
                          R"(route:%UPSTREAM_METADATA(["test.namespace", "key"])%)", append);

                if (route.has_route()) {
                  auto* route_action = route.mutable_route();
                  if (route_action->has_weighted_clusters()) {
                    for (auto& c : *route_action->mutable_weighted_clusters()->mutable_clusters()) {
                      addHeader(c.mutable_response_headers_to_add(), "x-weighted-cluster-dynamic",
                                R"(weighted:%UPSTREAM_METADATA(["test.namespace", "key"])%)",
                                append);
                    }
                  }
                }
              }
            }
          }

          hcm.mutable_normalize_path()->set_value(normalize_path_);
          hcm.set_path_with_escaped_slashes_action(path_with_escaped_slashes_action_);

          if (append) {
            // The config specifies append by default: no modifications needed.
            return;
          }

          // Iterate over VirtualHosts and nested Routes, disabling header append.
          for (auto& vhost : *route_config->mutable_virtual_hosts()) {
            disableHeaderValueOptionAppend(*vhost.mutable_request_headers_to_add());
            disableHeaderValueOptionAppend(*vhost.mutable_response_headers_to_add());

            for (auto& route : *vhost.mutable_routes()) {
              disableHeaderValueOptionAppend(*route.mutable_request_headers_to_add());
              disableHeaderValueOptionAppend(*route.mutable_response_headers_to_add());

              if (route.has_route()) {
                auto* route_action = route.mutable_route();

                if (route_action->has_weighted_clusters()) {
                  for (auto& c : *route_action->mutable_weighted_clusters()->mutable_clusters()) {
                    disableHeaderValueOptionAppend(*c.mutable_request_headers_to_add());
                    disableHeaderValueOptionAppend(*c.mutable_response_headers_to_add());
                  }
                }
              }
            }
          }
        });

    initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    if (use_eds_) {
      addFakeUpstream(Http::CodecType::HTTP2);
    }
  }

  void initialize() override {
    if (use_eds_) {
      on_server_init_function_ = [this]() {
        AssertionResult result =
            fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, eds_connection_);
        RELEASE_ASSERT(result, result.message());
        result = eds_connection_->waitForNewStream(*dispatcher_, eds_stream_);
        RELEASE_ASSERT(result, result.message());
        eds_stream_->startGrpcStream();

        envoy::service::discovery::v3::DiscoveryRequest discovery_request;
        result = eds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request);
        RELEASE_ASSERT(result, result.message());

        envoy::service::discovery::v3::DiscoveryResponse discovery_response;
        discovery_response.set_version_info("1");
        discovery_response.set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);

        auto cluster_load_assignment =
            TestUtility::parseYaml<envoy::config::endpoint::v3::ClusterLoadAssignment>(fmt::format(
                R"EOF(
                cluster_name: cluster_0
                endpoints:
                - lb_endpoints:
                  - endpoint:
                      address:
                        socket_address:
                          address: {}
                          port_value: {}
                    metadata:
                      filter_metadata:
                        test.namespace:
                          key: metadata-value
              )EOF",
                Network::Test::getLoopbackAddressString(std::get<0>(GetParam())),
                fake_upstreams_[0]->localAddress()->ip()->port()));

        discovery_response.add_resources()->PackFrom(cluster_load_assignment);
        eds_stream_->sendGrpcMessage(discovery_response);

        // Wait for the next request to make sure the first response was consumed.
        result = eds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request);
        RELEASE_ASSERT(result, result.message());
      };
    }

    HttpIntegrationTest::initialize();
  }

protected:
  void performRequest(Http::TestRequestHeaderMapImpl&& request_headers,
                      Http::TestRequestHeaderMapImpl&& expected_request_headers,
                      Http::TestResponseHeaderMapImpl&& response_headers,
                      Http::TestResponseHeaderMapImpl&& expected_response_headers) {
    registerTestServerPorts({"http"});
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
    auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

    compareHeaders(Http::TestRequestHeaderMapImpl(upstream_request_->headers()),
                   expected_request_headers);
    compareHeaders(Http::TestResponseHeaderMapImpl(response->headers()), expected_response_headers);
  }

  template <class Headers, class ExpectedHeaders>
  void compareHeaders(Headers&& headers, const ExpectedHeaders& expected_headers) {
    headers.remove(Envoy::Http::LowerCaseString{"content-length"});
    headers.remove(Envoy::Http::LowerCaseString{"date"});
    if (!routerSuppressEnvoyHeaders()) {
      headers.remove(Envoy::Http::LowerCaseString{"x-envoy-expected-rq-timeout-ms"});
      headers.remove(Envoy::Http::LowerCaseString{"x-envoy-upstream-service-time"});
    }
    headers.remove(Envoy::Http::LowerCaseString{"x-forwarded-proto"});
    headers.remove(Envoy::Http::LowerCaseString{"x-request-id"});
    headers.remove(Envoy::Http::LowerCaseString{"x-envoy-internal"});

    EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected_headers));
  }

  bool use_eds_{false};
  bool normalize_path_{false};
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      PathWithEscapedSlashesAction path_with_escaped_slashes_action_{
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
              KEEP_UNCHANGED};
  FakeHttpConnectionPtr eds_connection_;
  FakeStreamPtr eds_stream_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsSuppressEnvoyHeaders, HeaderIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipSuppressEnvoyHeadersTestParamsToString);

TEST_P(HeaderIntegrationTest, WeightedClusterWithClusterHeader) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        // Overwrite default config with our own.
        TestUtility::loadFromYaml(R"EOF(
http_filters:
  - name: envoy.filters.http.router
codec_type: HTTP1
use_remote_address: false
xff_num_trusted_hops: 1
stat_prefix: header_test
route_config:
  name: route-config-1
  virtual_hosts:
    - name: vhost-headers
      domains: ["vhost-headers.com"]
      routes:
        - match: { prefix: "/vhost-route-and-weighted-clusters" }
          name: route-0
          route:
            weighted_clusters:
              clusters:
                - cluster_header: x-route-to-this-cluster
                  weight: 100
                  request_headers_to_add:
                    - header:
                        key: "x-weighted-cluster-request"
                        value: "weighted-cluster-1"
                  request_headers_to_remove: ["x-weighted-cluster-request-remove"]
                  response_headers_to_add:
                    - header:
                        key: "x-weighted-cluster-response"
                        value: "weighted-cluster-1"
                  response_headers_to_remove: ["x-weighted-cluster-response-remove"]
)EOF",
                                  hcm);
        envoy::extensions::filters::http::router::v3::Router router_config;
        router_config.set_suppress_envoy_headers(routerSuppressEnvoyHeaders());
        hcm.mutable_http_filters(0)->mutable_typed_config()->PackFrom(router_config);
      });
  initialize();
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-weighted-cluster-request-remove", "to-remove"},
          {"x-route-to-this-cluster", "cluster_0"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-route-to-this-cluster", "cluster_0"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":method", "GET"},
          {"x-weighted-cluster-request", "weighted-cluster-1"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-weighted-cluster-response-remove", "to-remove"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {":status", "200"},
          {"x-weighted-cluster-response", "weighted-cluster-1"},
      });
}
// Validate that downstream request headers are passed upstream and upstream response headers are
// passed downstream.
TEST_P(HeaderIntegrationTest, TestRequestAndResponseHeaderPassThrough) {
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "no-headers.com"},
          {"x-request-foo", "downstram"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "no-headers.com"},
          {"x-request-foo", "downstram"},
          {":path", "/"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-return-foo", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-return-foo", "upstream"},
          {":status", "200"},
      });
}

// Validates the virtual host appends upstream request headers and appends/removes upstream
// response headers.
TEST_P(HeaderIntegrationTest, TestVirtualHostAppendHeaderManipulation) {
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-only"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-vhost-request-remove", "downstream"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-only"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response-remove", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the virtual host replaces request headers and replaces upstream response headers.
TEST_P(HeaderIntegrationTest, TestVirtualHostReplaceHeaderManipulation) {
  initializeFilter(HeaderMode::Replace, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-only"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-unmodified", "downstream"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "downstream"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-only"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-unmodified", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "upstream"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the route appends request headers and appends/removes upstream response headers.
TEST_P(HeaderIntegrationTest, TestRouteAppendHeaderManipulation) {
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/route-only"},
          {":scheme", "http"},
          {":authority", "route-headers.com"},
          {"x-route-request", "downstream"},
          {"x-route-request-remove", "downstream"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "route-headers.com"},
          {"x-route-request", "downstream"},
          {"x-route-request", "route"},
          {":path", "/route-only"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-route-response", "upstream"},
          {"x-route-response", "route"},
          {"details", "via_upstream"},
          {":status", "200"},
      });
}

// Validates the route replaces request headers and replaces/removes upstream response headers.
TEST_P(HeaderIntegrationTest, TestRouteReplaceHeaderManipulation) {
  initializeFilter(HeaderMode::Replace, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/route-only"},
          {":scheme", "http"},
          {":authority", "route-headers.com"},
          {"x-route-request", "downstream"},
          {"x-route-request-remove", "downstream"},
          {"x-unmodified", "downstream"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "route-headers.com"},
          {"x-unmodified", "downstream"},
          {"x-route-request", "route"},
          {":path", "/route-only"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
          {"x-unmodified", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "upstream"},
          {"x-route-response", "route"},
          {"details", "via_upstream"},
          {":status", "200"},
      });
}

// Validates the relationship between virtual host and route header manipulations when appending.
TEST_P(HeaderIntegrationTest, TestVirtualHostAndRouteAppendHeaderManipulation) {
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-vhost-request-remove", "downstream"},
          {"x-route-request", "downstream"},
          {"x-route-request-remove", "downstream"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response-remove", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the relationship between virtual host and route header manipulations when replacing.
TEST_P(HeaderIntegrationTest, TestVirtualHostAndRouteReplaceHeaderManipulation) {
  initializeFilter(HeaderMode::Replace, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-unmodified", "request"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "request"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the relationship between route configuration, virtual host and route header
// manipulations when appending.
TEST_P(HeaderIntegrationTest, TestRouteConfigVirtualHostAndRouteAppendHeaderManipulation) {
  initializeFilter(HeaderMode::Append, true);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-routeconfig-request-remove", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-vhost-request-remove", "downstream"},
          {"x-route-request", "downstream"},
          {"x-route-request-remove", "downstream"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {"x-routeconfig-request", "routeconfig"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-routeconfig-response", "upstream"},
          {"x-routeconfig-response-remove", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response-remove", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-routeconfig-response", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {"x-routeconfig-response", "routeconfig"},
          {":status", "200"},
      });
}

// Validates the relationship between route configuration, virtual host and route header
// manipulations when replacing.
TEST_P(HeaderIntegrationTest, TestRouteConfigVirtualHostAndRouteReplaceHeaderManipulation) {
  initializeFilter(HeaderMode::Replace, true);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-unmodified", "request"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "request"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {"x-routeconfig-request", "routeconfig"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-routeconfig-response", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {"x-routeconfig-response", "routeconfig"},
          {":status", "200"},
      });
}

// Validates the relationship between route configuration, virtual host, route, and weighted
// cluster header manipulations when appending.
TEST_P(HeaderIntegrationTest, TestRouteConfigVirtualHostRouteAndClusterAppendHeaderManipulation) {
  initializeFilter(HeaderMode::Append, true);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-routeconfig-request-remove", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-vhost-request-remove", "downstream"},
          {"x-route-request", "downstream"},
          {"x-route-request-remove", "downstream"},
          {"x-weighted-cluster-request", "downstream"},
          {"x-weighted-cluster-request-remove", "downstream"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-weighted-cluster-request", "downstream"},
          {"x-weighted-cluster-request", "weighted-cluster-1"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {"x-routeconfig-request", "routeconfig"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-routeconfig-response", "upstream"},
          {"x-routeconfig-response-remove", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response-remove", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
          {"x-weighted-cluster-response", "upstream"},
          {"x-weighted-cluster-response-remove", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-routeconfig-response", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-weighted-cluster-response", "upstream"},
          {"x-weighted-cluster-response", "weighted-cluster-1"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {"x-routeconfig-response", "routeconfig"},
          {":status", "200"},
      });
}

// Validates the relationship between route configuration, virtual host, route and weighted cluster
// header manipulations when replacing.
TEST_P(HeaderIntegrationTest, TestRouteConfigVirtualHostRouteAndClusterReplaceHeaderManipulation) {
  initializeFilter(HeaderMode::Replace, true);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-weighted-cluster-request", "downstream"},
          {"x-unmodified", "request"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "request"},
          {"x-weighted-cluster-request", "weighted-cluster-1"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {"x-routeconfig-request", "routeconfig"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-routeconfig-response", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-weighted-cluster-response", "upstream"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {"x-weighted-cluster-response", "weighted-cluster-1"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {"x-routeconfig-response", "routeconfig"},
          {":status", "200"},
      });
}

// Validates that upstream host metadata can be emitted in headers.
TEST_P(HeaderIntegrationTest, TestDynamicHeaders) {
  prepareEDS();
  initializeFilter(HeaderMode::Replace, true);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-weighted-cluster-request", "downstream"},
          {"x-unmodified", "request"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "request"},
          {"x-weighted-cluster-request", "weighted-cluster-1"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {"x-routeconfig-request", "routeconfig"},
          {":path", "/vhost-route-and-weighted-clusters"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-routeconfig-response", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-weighted-cluster-response", "upstream"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {"x-weighted-cluster-response", "weighted-cluster-1"},
          {"x-weighted-cluster-dynamic", "weighted:metadata-value"},
          {"x-route-response", "route"},
          {"x-route-dynamic", "route:metadata-value"},
          {"x-vhost-response", "vhost"},
          {"x-vhost-dynamic", "vhost:metadata-value"},
          {"x-routeconfig-response", "routeconfig"},
          {"x-routeconfig-dynamic", "metadata-value"},
          {":status", "200"},
      });
}

// Validates that XFF gets properly parsed.
TEST_P(HeaderIntegrationTest, TestXFFParsing) {
  initializeFilter(HeaderMode::Replace, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "xff-headers.com"},
          {"x-forwarded-for", "1.2.3.4, 5.6.7.8 ,9.10.11.12"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "xff-headers.com"},
          {"x-forwarded-for", "1.2.3.4, 5.6.7.8 ,9.10.11.12"},
          {"x-real-ip", "5.6.7.8"},
          {":path", "/test"},
          {":method", "GET"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {":status", "200"},
      });
}

// Validates behavior around same header appending (both predefined headers and
// other).
TEST_P(HeaderIntegrationTest, TestAppendSameHeaders) {
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "append-same-headers.com"},
          {"user-agent", "token3"},
          {"x-foo", "value3"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "append-same-headers.com"},
          {":path", "/test"},
          {":method", "GET"},
          {"user-agent", "token3,token2,token1"},
          {"x-foo", "value3"},
          {"x-foo", "value2"},
          {"x-foo", "value1"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {":status", "200"},
      });
}

// Validates behavior when normalize path is off.
// Route selection and path to upstream are the exact string literal
// from downstream.
TEST_P(HeaderIntegrationTest, TestPathAndRouteWhenNormalizePathOff) {
  normalize_path_ = false;
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private/../public"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      },
      Http::TestRequestHeaderMapImpl{{":authority", "path-sanitization.com"},
                                     {":path", "/private/../public"},
                                     {":method", "GET"},
                                     {"x-site", "private"}},
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {":status", "200"},
      });
}

// Validates behavior when normalize path is on.
// Path to decide route and path to upstream are both
// the normalized.
TEST_P(HeaderIntegrationTest, TestPathAndRouteOnNormalizedPath) {
  normalize_path_ = true;
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private/../public"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      },
      Http::TestRequestHeaderMapImpl{{":authority", "path-sanitization.com"},
                                     {":path", "/public"},
                                     {":method", "GET"},
                                     {"x-site", "public"}},
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {":status", "200"},
      });
}

// Validates that Envoy by default does not modify escaped slashes.
TEST_P(HeaderIntegrationTest, PathWithEscapedSlashesByDefaultUnchanghed) {
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::IMPLEMENTATION_SPECIFIC_DEFAULT;
  normalize_path_ = true;
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private/..%2Fpublic%5c"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      },
      Http::TestRequestHeaderMapImpl{{":authority", "path-sanitization.com"},
                                     {":path", "/private/..%2Fpublic%5c"},
                                     {":method", "GET"},
                                     {"x-site", "private"}},
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {":status", "200"},
      });
}

// Validates that default action can be overridden through runtime.
TEST_P(HeaderIntegrationTest, PathWithEscapedSlashesDefaultOverriden) {
  // Override the default action to REJECT
  config_helper_.addRuntimeOverride("http_connection_manager.path_with_escaped_slashes_action",
                                    "2");
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::IMPLEMENTATION_SPECIFIC_DEFAULT;
  initializeFilter(HeaderMode::Append, false);
  registerTestServerPorts({"http"});
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private%2f../public"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      });
  EXPECT_TRUE(response->waitForEndStream());
  Http::TestResponseHeaderMapImpl response_headers{response->headers()};
  compareHeaders(response_headers, Http::TestResponseHeaderMapImpl{
                                       {"server", "envoy"},
                                       {"connection", "close"},
                                       {":status", "400"},
                                   });
}

TEST_P(HeaderIntegrationTest, PathWithEscapedSlashesRejected) {
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::REJECT_REQUEST;
  initializeFilter(HeaderMode::Append, false);
  registerTestServerPorts({"http"});
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private%2f../public"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      });
  EXPECT_TRUE(response->waitForEndStream());
  Http::TestResponseHeaderMapImpl response_headers{response->headers()};
  compareHeaders(response_headers, Http::TestResponseHeaderMapImpl{
                                       {"server", "envoy"},
                                       {"connection", "close"},
                                       {":status", "400"},
                                   });
}

// Validates that Envoy does not modify escaped slashes when configured.
TEST_P(HeaderIntegrationTest, PathWithEscapedSlashesUnmodified) {
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::KEEP_UNCHANGED;
  normalize_path_ = true;
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private/..%2Fpublic%5c"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      },
      Http::TestRequestHeaderMapImpl{{":authority", "path-sanitization.com"},
                                     {":path", "/private/..%2Fpublic%5c"},
                                     {":method", "GET"},
                                     {"x-site", "private"}},
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {":status", "200"},
      });
}

// Validates that Envoy forwards unescaped slashes when configured.
TEST_P(HeaderIntegrationTest, PathWithEscapedSlashesAndNormalizationForwarded) {
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::UNESCAPE_AND_FORWARD;
  normalize_path_ = true;
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private/..%2Fpublic%5c%2e%2Fabc"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      },
      Http::TestRequestHeaderMapImpl{{":authority", "path-sanitization.com"},
                                     {":path", "/public/abc"},
                                     {":method", "GET"},
                                     {"x-site", "public"}},
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-unmodified", "response"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {":status", "200"},
      });
}

TEST_P(HeaderIntegrationTest, PathWithEscapedSlashesRedirected) {
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::UNESCAPE_AND_REDIRECT;
  initializeFilter(HeaderMode::Append, false);
  registerTestServerPorts({"http"});
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/private%2f../%2e%5Cpublic"},
          {":scheme", "http"},
          {":authority", "path-sanitization.com"},
      });
  EXPECT_TRUE(response->waitForEndStream());
  Http::TestResponseHeaderMapImpl response_headers{response->headers()};
  compareHeaders(response_headers, Http::TestResponseHeaderMapImpl{
                                       {"server", "envoy"},
                                       {"location", "/private/../%2e\\public"},
                                       {":status", "307"},
                                   });
}

// Validates TE header is forwarded if it contains a supported value
TEST_P(HeaderIntegrationTest, TestTeHeaderPassthrough) {
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "no-headers.com"},
          {"x-request-foo", "downstram"},
          {"connection", "te, close"},
          {"te", "trailers"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "no-headers.com"},
          {":path", "/"},
          {":method", "GET"},
          {"x-request-foo", "downstram"},
          {"te", "trailers"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-return-foo", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-return-foo", "upstream"},
          {":status", "200"},
          {"connection", "close"},
      });
}

// Validates TE header is stripped if it contains an unsupported value
TEST_P(HeaderIntegrationTest, TestTeHeaderSanitized) {
  initializeFilter(HeaderMode::Append, false);
  performRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "no-headers.com"},
          {"x-request-foo", "downstram"},
          {"connection", "te, mike, sam, will, close"},
          {"te", "gzip"},
          {"mike", "foo"},
          {"sam", "bar"},
          {"will", "baz"},
      },
      Http::TestRequestHeaderMapImpl{
          {":authority", "no-headers.com"},
          {":path", "/"},
          {":method", "GET"},
          {"x-request-foo", "downstram"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-return-foo", "upstream"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"x-return-foo", "upstream"},
          {":status", "200"},
          {"connection", "close"},
      });
}

using EmptyHeaderIntegrationTest = HttpProtocolIntegrationTest;
using HeaderValueOption = envoy::config::core::v3::HeaderValueOption;

INSTANTIATE_TEST_SUITE_P(Protocols, EmptyHeaderIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(EmptyHeaderIntegrationTest, AllProtocolsPassEmptyHeaders) {
  auto vhost = config_helper_.createVirtualHost("empty-headers.com");
  *vhost.add_request_headers_to_add() = TestUtility::parseYaml<HeaderValueOption>(R"EOF(
    header:
      key: "x-ds-add-empty"
      value: "%PER_REQUEST_STATE(does.not.exist)%"
    keep_empty_value: true
  )EOF");
  *vhost.add_request_headers_to_add() = TestUtility::parseYaml<HeaderValueOption>(R"EOF(
    header:
      key: "x-ds-no-add-empty"
      value: "%PER_REQUEST_STATE(does.not.exist)%"
  )EOF");
  *vhost.add_response_headers_to_add() = TestUtility::parseYaml<HeaderValueOption>(R"EOF(
    header:
      key: "x-us-add-empty"
      value: "%PER_REQUEST_STATE(does.not.exist)%"
    keep_empty_value: true
  )EOF");
  *vhost.add_response_headers_to_add() = TestUtility::parseYaml<HeaderValueOption>(R"EOF(
    header:
      key: "x-us-no-add-empty"
      value: "%PER_REQUEST_STATE(does.not.exist)%"
  )EOF");

  config_helper_.addVirtualHost(vhost);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "empty-headers.com"}},
      0,
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"}, {"content-length", "0"}, {":status", "200"}},
      0);
  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("x-ds-add-empty")).size(), 1);
  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("x-ds-no-add-empty")).empty());
  EXPECT_EQ(response->headers().get(Http::LowerCaseString("x-us-add-empty")).size(), 1);
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("x-us-no-add-empty")).empty());
}
} // namespace Envoy
