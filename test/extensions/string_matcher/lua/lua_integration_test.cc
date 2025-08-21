#include "envoy/extensions/string_matcher/lua/v3/lua.pb.h"

#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace String {
namespace Lua {

class LuaIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  LuaIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    autonomous_upstream_ = true;
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          ::envoy::extensions::string_matcher::lua::v3::Lua config;
          config.mutable_source_code()->set_inline_string(
              R"(
              function envoy_match(str)
                return str == "good" or str == "acceptable"
              end
            )");

          auto* header_match = hcm.mutable_route_config()
                                   ->mutable_virtual_hosts(0)
                                   ->mutable_routes(0)
                                   ->mutable_match()
                                   ->add_headers();

          header_match->set_name("lua-header");

          auto* string_match_extension = header_match->mutable_string_match()->mutable_custom();
          string_match_extension->set_name("unused but must be set");
          string_match_extension->mutable_typed_config()->PackFrom(config);
        });
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LuaIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(LuaIntegrationTest, HeaderMatcher) {
  Http::TestRequestHeaderMapImpl matching_request_headers{
      {":method", "GET"},           {":path", "/"},
      {":scheme", "http"},          {":authority", "example.com"},
      {"lua-header", "acceptable"},
  };
  codec_client_ = makeHttpConnection(lookupPort("http"));
  {
    auto response = codec_client_->makeHeaderOnlyRequest(matching_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }

  Http::TestRequestHeaderMapImpl non_matching_request_headers{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "example.com"},
      {"lua-header", "unacceptable"},
  };
  {
    auto response = codec_client_->makeHeaderOnlyRequest(non_matching_request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("404", response->headers().Status()->value().getStringView());
  }
}

} // namespace Lua
} // namespace String
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
