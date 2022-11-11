#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.h"
#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.validate.h"

#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace {

using ProtoRegexMutation =
    envoy::extensions::http::early_header_mutation::regex_mutation::v3::RegexMutation;

class RegexMutationIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  RegexMutationIntegrationTest() : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()) {}

  void initializeExtension(const std::string& regex_mutation_yaml) {
    config_helper_.addConfigModifier(
        [regex_mutation_yaml](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          ProtoRegexMutation proto_mutation;
          TestUtility::loadFromYaml(regex_mutation_yaml, proto_mutation);

          // Load extension.
          auto* new_extension = hcm.add_early_header_mutation_extensions();
          new_extension->set_name("test");
          new_extension->mutable_typed_config()->PackFrom(proto_mutation);

          // Update route to make sure only requests that have path start with "/prefix" will be
          // processed by the HCM.
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_match()
              ->set_prefix("/prefix");
        });

    HttpIntegrationTest::initialize();
  }

  ScopedInjectableLoader<Regex::Engine> engine_{std::make_unique<Regex::GoogleReEngine>()};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RegexMutationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(RegexMutationIntegrationTest, TestRegexMutation) {
  const std::string regex_mutation_yaml = R"EOF(
  header_mutations:
    - header: "flag-header"
      rename: ":path"          # Try to reset path.
      regex_rewrite:
        pattern:
          regex: "^reset-path$"
        substitution: "/prefix"
  )EOF";

  initializeExtension(regex_mutation_yaml);

  // Not reset path by the regex mutation and get 404.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Envoy::Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                          {":path", "/suffix"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"flag-header", "no-reset-path"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("404", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Envoy::Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                          {":path", "/suffix"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"flag-header", "reset-path"}};
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
