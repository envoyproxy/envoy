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
    config_helper_.addConfigModifier([this](ConfigHelper::HttpConnectionManager& cm) {
      auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
      auto* route = vh->mutable_routes()->Mutable(0);
      route->mutable_match()->set_path("/");
      Any cfg_any;
      const auto per_route_matcher = TestUtility::parseYaml<
          envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute>(per_route_config_);

      ASSERT_TRUE(cfg_any.PackFrom(per_route_matcher));
      route->mutable_typed_per_filter_config()->insert(
          MapPair<std::string, Any>("envoy.filters.http.match_delegate", cfg_any));
    });

    config_helper_.prependFilter(default_config_);
    HttpIntegrationTest::initialize();
  }

  IntegrationStreamDecoderPtr response_;
  FakeStreamPtr request_{};
  envoy::extensions::common::matching::v3::ExtensionWithMatcher matcher_;
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
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseHeaderMatchInput
              header_name: default-matcher-header
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
            match:
              action:
                name: set-response-code
                typed_config:
                  "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
                  code: 200
  )EOF";
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MatchDelegateInegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MatchDelegateInegrationTest, Basicflow) {
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response_ = codec_client_->makeHeaderOnlyRequest(Envoy::Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "test"}, {":scheme", "http"}, {":authority", "host"}});
  EXPECT_THAT(response_->headers(), Envoy::Http::HttpStatusIs("403"));
  response_ = codec_client_->makeHeaderOnlyRequest(Envoy::Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});
  EXPECT_THAT(response_->headers(), Envoy::Http::HttpStatusIs("200"));
}
} // namespace
} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
