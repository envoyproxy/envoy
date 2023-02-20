#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "contrib/golang/http/cluster_specifier/source/config.h"
#include "contrib/golang/http/cluster_specifier/source/golang_cluster_specifier.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace Golang {
namespace {

class GolangClusterSpecifierIntegrationTest : public Envoy::HttpIntegrationTest,
                                              public testing::Test {
public:
  GolangClusterSpecifierIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void initializeRoute(const std::string& vhost_config_yaml) {
    envoy::config::route::v3::VirtualHost vhost;
    TestUtility::loadFromYaml(vhost_config_yaml, vhost);
    config_helper_.addVirtualHost(vhost);
    initialize();
  }
};

static const auto yaml_fmt =
    R"EOF(
name: test_golang_cluster_specifier_plugin
domains:
- test.com
routes:
- name: test_route_1
  match:
    prefix: /
  route:
    inline_cluster_specifier_plugin:
      extension:
        name: golang
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.cluster_specifier.golang.v3alpha.Config
          library_id: %s
          library_path: %s
          config:
            "@type": type.googleapis.com/xds.type.v3.TypedStruct
            value:
              invalid_prefix: "/admin/"
)EOF";

std::string genSoPath(std::string name) {
  return TestEnvironment::substitute(
      "{{ test_rundir }}/contrib/golang/http/cluster_specifier/test/test_data/" + name +
      "/plugin.so");
}

// return the default cluster: "cluster_0"
TEST_F(GolangClusterSpecifierIntegrationTest, OK) {
  auto so_id = "simple";
  auto yaml_string = absl::StrFormat(yaml_fmt, so_id, genSoPath(so_id));
  initializeRoute(yaml_string);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestResponseHeaderMapImpl response_headers{
      {"server", "envoy"},
      {":status", "200"},
  };

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "test.com"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  cleanupUpstreamAndDownstream();
}

// return the unknown cluster: "cluster_unknown"
TEST_F(GolangClusterSpecifierIntegrationTest, UnknownCluster) {
  auto so_id = "simple";
  auto yaml_string = absl::StrFormat(yaml_fmt, so_id, genSoPath(so_id));
  initializeRoute(yaml_string);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/admin/user"},
                                                 {":scheme", "http"},
                                                 {":authority", "test.com"}};

  // Request with the "/admin/" prefix URI, unknown cluster name will be return by the cluster
  // specifier plugin.
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("503"));

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Golang
} // namespace Router
} // namespace Envoy
