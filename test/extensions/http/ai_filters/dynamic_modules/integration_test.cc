#include <filesystem>
#include <string>

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/type/ai/v3/llm_protocol.pb.h"

#include "source/common/common/fmt.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace {

constexpr absl::string_view ChatPayload =
    R"({"model":"gpt-4o","temperature":0.5,"messages":[{"role":"user","content":"hi"}]})";

std::string headerValue(const Http::RequestHeaderMap& headers, absl::string_view key) {
  const auto values = headers.get(Http::LowerCaseString(key));
  return values.empty() ? "" : std::string(values[0]->value().getStringView());
}

// Drives the extension end to end: AI Protocol Manager -> factory -> AI filter -> ABI -> module.
// The module is a parameter, (language, module name), so the assertions cover every SDK.
class DynamicModuleAiFilterIntegrationTest
    : public testing::TestWithParam<std::pair<std::string, std::string>>,
      public HttpIntegrationTest {
public:
  DynamicModuleAiFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  static std::string
  paramName(const testing::TestParamInfo<std::pair<std::string, std::string>>& info) {
    return info.param.first;
  }

  void initializeFilter(absl::string_view filter_name) {
    const auto& [language, module_name] = GetParam();
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        std::filesystem::path(
            Extensions::DynamicModules::testSharedObjectPath(module_name, language))
            .parent_path()
            .string(),
        1);

    config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
          per_route;
      per_route.mutable_request()->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
      auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
      std::ignore =
          (*route->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
              .PackFrom(per_route);
    });
    config_helper_.prependFilter(fmt::format(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  request_handling: {{}}
  filters:
  - name: envoy.http.ai_filters.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.ai_filters.dynamic_modules.v3.DynamicModuleAiFilter
      dynamic_module_config:
        name: {}
        do_not_close: true
      filter_name: {}
)EOF",
                                             module_name, filter_name));
    initialize();
  }

  IntegrationStreamDecoderPtr sendChatRequest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    return codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/v1/chat/completions"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"},
                                       {"x-tenant", "acme"},
                                       {"x-remove-me", "1"}},
        std::string(ChatPayload));
  }
};

INSTANTIATE_TEST_SUITE_P(Modules, DynamicModuleAiFilterIntegrationTest,
                         testing::Values(std::make_pair("c", "ai_filter"),
                                         std::make_pair("rust", "ai_filter_integration_test")),
                         DynamicModuleAiFilterIntegrationTest::paramName);

TEST_P(DynamicModuleAiFilterIntegrationTest, RewritesTheRequestSentUpstream) {
  initializeFilter("rewrite");
  IntegrationStreamDecoderPtr response = sendChatRequest();

  waitForNextUpstreamRequest();
  EXPECT_EQ(nlohmann::json::parse(upstream_request_->body().toString()), nlohmann::json::parse(R"({
    "model": "rewritten",
    "metadata": {"tagged": true},
    "messages": [{"role": "user", "content": "hi"}, {"role": "system", "content": "appended"}]
  })"));
  const Http::RequestHeaderMap& headers = upstream_request_->headers();
  EXPECT_EQ(headerValue(headers, "x-protocol"), "OPENAI_CHAT_COMPLETIONS");
  EXPECT_EQ(headerValue(headers, "x-model"), "\"gpt-4o\"");
  EXPECT_EQ(headerValue(headers, "x-tenant-echo"), "acme");
  EXPECT_EQ(headerValue(headers, "x-remove-me"), "");

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");
}

TEST_P(DynamicModuleAiFilterIntegrationTest, LocalReplyRejectsTheRequest) {
  initializeFilter("local_reply");
  IntegrationStreamDecoderPtr response = sendChatRequest();

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "403");
  EXPECT_EQ(response->body(), "blocked");
}

TEST_P(DynamicModuleAiFilterIntegrationTest, ErrorFailsTheRequest) {
  initializeFilter("error");
  IntegrationStreamDecoderPtr response = sendChatRequest();

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "502");
}

// The C module returns Error and the Rust module panics; neither lets the request pass unfiltered.
TEST_P(DynamicModuleAiFilterIntegrationTest, FailedFilterCreationFailsTheRequest) {
  initializeFilter("fail_new");
  IntegrationStreamDecoderPtr response = sendChatRequest();

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "502");
}

} // namespace
} // namespace Envoy
