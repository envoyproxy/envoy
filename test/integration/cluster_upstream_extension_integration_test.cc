#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/buffer/buffer_impl.h"


// #include "test/integration/upstreams/per_host_upstream_config.h"
#include "test/integration/http_integration.h"

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
//      ->PackFrom(...);
      populateMetadataTestData(*cluster->mutable_metadata(), "foo",
                               "bar", "cluster-value");
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterUpstreamExtensionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClusterUpstreamExtensionIntegrationTest, VerifyCasedHeaders) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  auto request = "GET / HTTP/1.1\r\nhost: host\r\nmy-header: foo\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has proper cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  ENVOY_LOG_MISC(debug, "lambdai: upstream request = {}", upstream_request);
  EXPECT_TRUE(absl::StrContains(upstream_request, "X-cluster-foo: cluster-value"));

//   // Verify that the downstream response has proper cased headers.
//   auto response =
//       "HTTP/1.1 503 Service Unavailable\r\ncontent-length: 0\r\nresponse-header: foo\r\n\r\n";
//   ASSERT_TRUE(upstream_connection->write(response));

//   // Verify that we're at least one proper cased header.
//   tcp_client->waitForData("HTTP/1.1 503 Service Unavailable\r\nContent-Length:", true);

  tcp_client->close();
}
} // namespace
} // namespace Envoy
