#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "common/buffer/buffer_impl.h"

#include "test/integration/http_integration.h"
#include "test/integration/upstreams/per_host_upstream_config.h"
#include "test/test_common/registry.h"

#include "fake_upstream.h"
#include "gtest/gtest.h"

namespace Envoy {

namespace {
class ClusterUpstreamExtensionIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ClusterUpstreamExtensionIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  }
  
  void populateMetadataTestData(envoy::config::core::v3::Metadata& metadata, const std::string& k1,
                                const std::string& k2, const std::string& value) {

    ProtobufWkt::Struct struct_obj;
    (*struct_obj.mutable_fields())[k2] = ValueUtil::stringValue(value);

    (*metadata.mutable_filter_metadata())[k1] = struct_obj;
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { UNREFERENCED_PARAMETER(hcm); });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster->mutable_upstream_config()->set_name("envoy.filters.connection_pools.http.per_host");
      cluster->mutable_upstream_config()->mutable_typed_config();
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

TEST_P(ClusterUpstreamExtensionIntegrationTest,
       VerifyRequestHeadersAreRewrittenByClusterAndHostMetadata) {
  initialize();
  Registry::InjectFactory<Router::GenericConnPoolFactory> registration(per_host_upstream_factory_);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
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

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}
} // namespace
} // namespace Envoy
