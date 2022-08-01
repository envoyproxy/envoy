#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/upstreams/per_host_upstream_config.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace {
class ClusterUpstreamExtensionIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ClusterUpstreamExtensionIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void populateMetadataTestData(envoy::config::core::v3::Metadata& metadata,
                                const std::string& key1, const std::string& key2,
                                const std::string& value) {

    ProtobufWkt::Struct struct_obj;
    (*struct_obj.mutable_fields())[key2] = ValueUtil::stringValue(value);
    (*metadata.mutable_filter_metadata())[key1] = struct_obj;
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster->mutable_upstream_config()->set_name("envoy.filters.connection_pools.http.per_host");
      cluster->mutable_upstream_config()->mutable_typed_config()->set_type_url(
          "type.googleapis.com/google.protobuf.Struct");
      populateMetadataTestData(*cluster->mutable_metadata(), "foo", "bar", "cluster-value");
      populateMetadataTestData(*cluster->mutable_load_assignment()
                                    ->mutable_endpoints(0)
                                    ->mutable_lb_endpoints(0)
                                    ->mutable_metadata(),
                               "foo", "bar", "host-value");
    });
    HttpIntegrationTest::initialize();
  }
  PerHostGenericConnPoolFactory per_host_upstream_factory_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterUpstreamExtensionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// This test verifies that cluster upstream extensions can fulfill the requirement that they rewrite
// http headers after cluster and host are selected. See
// https://github.com/envoyproxy/envoy/issues/12236 This test case should be rewritten once upstream
// http filters(https://github.com/envoyproxy/envoy/issues/10455) is landed.
TEST_P(ClusterUpstreamExtensionIntegrationTest,
       VerifyRequestHeadersAreRewrittenByClusterAndHostMetadata) {
  initialize();
  Registry::InjectFactory<Router::GenericConnPoolFactory> registration(per_host_upstream_factory_);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0, Http::TestResponseHeaderMapImpl{{":status", "200"}}, 0);
  EXPECT_TRUE(upstream_request_->complete());

  {
    const auto header_values = upstream_request_->headers().get(Http::LowerCaseString("X-foo"));
    ASSERT_EQ(1, header_values.size());
    EXPECT_EQ("foo-common", header_values[0]->value().getStringView());
  }
  {
    const auto cluster_header_values =
        upstream_request_->headers().get(Http::LowerCaseString("X-cluster-foo"));
    ASSERT_EQ(1, cluster_header_values.size());
    EXPECT_EQ("cluster-value", cluster_header_values[0]->value().getStringView());
  }
  {
    const auto host_header_values =
        upstream_request_->headers().get(Http::LowerCaseString("X-host-foo"));
    ASSERT_EQ(1, host_header_values.size());
    EXPECT_EQ("host-value", host_header_values[0]->value().getStringView());
  }

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}
} // namespace
} // namespace Envoy
