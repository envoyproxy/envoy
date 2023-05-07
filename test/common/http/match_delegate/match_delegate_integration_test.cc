#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

// #include "envoy/extensions/common/matching/v3/extension_matcher.h"

#include "test/integration/filters/set_response_code_filter_config.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"

#include "source/common/http/match_delegate/config.h"

#include "test/integration/http_integration.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchDelegate {
namespace {

using Envoy::Protobuf::MapPair;
using Envoy::ProtobufWkt::Any;

class MatchDelegateInegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  MatchDelegateInegrationTest() : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()) {}
  void initialize() override {
    config_helper_.prependFilter(default_config_);
    HttpIntegrationTest::initialize();
  }

  IntegrationStreamDecoderPtr response_;
  FakeStreamPtr request_{};
  // envoy::extensions::common::matching::v3::ExtensionWithMatcher matcher_;
  const std::string default_config_ = R"EOF(
      name: ext_proc
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: set-response-code
          typed_config:
            "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
            code: 403
        xds_matcher:
          matcher_tree:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: match-header
            exact_match_map:
              map:
                match:
                  action:
                    name: skip
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
    )EOF";
  const std::string per_route_config_ = R"EOF(
      xds_matcher:
        matcher_tree:
          input:
            name: request-headers
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: match-header
          exact_match_map:
            map:
              route:
                action:
                  name: skip
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
    )EOF";
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MatchDelegateInegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MatchDelegateInegrationTest, BasicFlow) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response1 = codec_client_->makeHeaderOnlyRequest(
      Envoy::Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                            {":path", "/"},
                                            {":scheme", "http"},
                                            {"match-header", "route"},
                                            {":authority", "host"}});
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_THAT(response1->headers(), Envoy::Http::HttpStatusIs("403"));
}

TEST_P(MatchDelegateInegrationTest, PerRouteConfig) {
  config_helper_.addConfigModifier([this](ConfigHelper::HttpConnectionManager& cm) {
    auto* config = cm.mutable_route_config()
                       ->mutable_virtual_hosts()
                       ->Mutable(0)
                       ->mutable_typed_per_filter_config();
    const auto per_route_matcher = TestUtility::parseYaml<
        envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute>(per_route_config_);
    (*config)["envoy.filters.http.match_delegate"].PackFrom(per_route_matcher);
  });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Envoy::Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {"match-header", "route"},
                                                        {":authority", "host"}};
  Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response2 = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_THAT(response2->headers(), Envoy::Http::HttpStatusIs("200"));
}
} // namespace
} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
