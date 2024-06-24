#include "envoy/extensions/http/early_header_mutation/header_mutation/v3/header_mutation.pb.h"
#include "envoy/extensions/http/early_header_mutation/header_mutation/v3/header_mutation.pb.validate.h"

#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace {

using ProtoHeaderMutation =
    envoy::extensions::http::early_header_mutation::header_mutation::v3::HeaderMutation;

class HeaderMutationIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  HeaderMutationIntegrationTest()
      : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()) {}

  void initializeExtension(const std::string& header_mutation_yaml) {
    config_helper_.addConfigModifier(
        [header_mutation_yaml](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          ProtoHeaderMutation proto_mutation;
          TestUtility::loadFromYaml(header_mutation_yaml, proto_mutation);

          // Load extension.
          auto* new_extension = hcm.add_early_header_mutation_extensions();
          new_extension->set_name("test");
          new_extension->mutable_typed_config()->PackFrom(proto_mutation);

          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_match()
              ->set_prefix("/prefix");

          auto header = hcm.mutable_route_config()
                            ->mutable_virtual_hosts(0)
                            ->mutable_routes(0)
                            ->mutable_match()
                            ->add_headers();
          header->set_name("flag-header");
          header->mutable_string_match()->set_exact("true");
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HeaderMutationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HeaderMutationIntegrationTest, TestHeaderMutation) {
  const std::string header_mutation_yaml = R"EOF(
  mutations:
  - append:
      header:
        key: "flag-header"
        value: "%REQ(ANOTHER-FLAG-HEADER)%"
      append_action: OVERWRITE_IF_EXISTS_OR_ADD
  )EOF";

  initializeExtension(header_mutation_yaml);

  // Not reset path by the regex mutation and get 404.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Envoy::Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                          {":path", "/prefix"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"flag-header", "false"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("404", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Envoy::Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},     {":path", "/prefix"},     {":scheme", "http"},
        {":authority", "host"}, {"flag-header", "false"}, {"another-flag-header", "true"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(0, response->body().size());
  }
}

} // namespace
} // namespace Envoy
