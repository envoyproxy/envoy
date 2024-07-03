#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/printers.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class InlineScopedRoutesIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  InlineScopedRoutesIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void setScopedRoutesConfig(absl::string_view config_yaml) {
    config_helper_.addConfigModifier(
        [config_yaml](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes*
              scoped_routes = hcm.mutable_scoped_routes();
          const std::string scoped_routes_yaml = absl::StrCat(R"EOF(
name: foo-scoped-routes
scope_key_builder:
  fragments:
    - header_value_extractor:
        name: Addr
        element_separator: ;
        element:
          key: x-foo-key
          separator: =
)EOF",
                                                              config_yaml);
          TestUtility::loadFromYaml(scoped_routes_yaml, *scoped_routes);
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, InlineScopedRoutesIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(InlineScopedRoutesIntegrationTest, NoScopeFound) {
  absl::string_view config_yaml = R"EOF(
scoped_route_configurations_list:
  scoped_route_configurations:
    - name: foo-scope
      route_configuration:
        name: foo
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
      key:
        fragments: { string_key: foo }
)EOF";
  setScopedRoutesConfig(config_yaml);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     // "xyz-route" is not a configured scope key.
                                     {"Addr", "x-foo-key=xyz-route"}});
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "404", Http::TestResponseHeaderMapImpl{}, "");
  cleanupUpstreamAndDownstream();
}

TEST_P(InlineScopedRoutesIntegrationTest, ScopeWithSingleRouteConfiguration) {
  absl::string_view config_yaml = R"EOF(
scoped_route_configurations_list:
  scoped_route_configurations:
    - name: foo-scope
      route_configuration:
        name: foo
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
      key:
        fragments: { string_key: foo }
)EOF";
  setScopedRoutesConfig(config_yaml);
  initialize();

  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}, {"service", "foo"}},
      /*response_size=*/0,
      /*backend_idx=*/0);
}

TEST_P(InlineScopedRoutesIntegrationTest, ScopeWithMultipleRouteConfigurations) {
  absl::string_view config_yaml = R"EOF(
scoped_route_configurations_list:
  scoped_route_configurations:
    - name: foo-scope
      route_configuration:
        name: foo
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
            response_headers_to_add:
              header: { key: route-name, value: foo }
      key:
        fragments: { string_key: foo }
    - name: baz-scope
      route_configuration:
        name: baz
        virtual_hosts:
          - name: bar
            domains: ["*"]
            routes:
              - match: { prefix: "/" }
                route: { cluster: cluster_0 }
            response_headers_to_add:
              header: { key: route-name, value: baz }
      key:
        fragments: { string_key: baz }

)EOF";
  setScopedRoutesConfig(config_yaml);
  initialize();

  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=baz"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}},
      /*response_size=*/0,
      /*backend_idx=*/0, Http::TestResponseHeaderMapImpl{{"route-name", "baz"}});
  sendRequestAndVerifyResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":authority", "host"},
                                     {":scheme", "http"},
                                     {"Addr", "x-foo-key=foo"}},
      /*request_size=*/0, Http::TestResponseHeaderMapImpl{{":status", "200"}},
      /*response_size=*/0,
      /*backend_idx=*/0, Http::TestResponseHeaderMapImpl{{"route-name", "foo"}});
  ;
}
} // namespace
} // namespace Envoy
