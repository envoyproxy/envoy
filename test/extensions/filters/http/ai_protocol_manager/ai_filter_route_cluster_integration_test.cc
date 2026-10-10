#include <memory>
#include <string>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"

#include "source/common/coroutine/status_macros.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace {

using Extensions::HttpFilters::AiProtocolManager::AiFilter;
using Extensions::HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using Extensions::HttpFilters::AiProtocolManager::AiFilterContext;
using Extensions::HttpFilters::AiProtocolManager::AiFilterFactoryCb;
using Extensions::HttpFilters::AiProtocolManager::AiFilterSharedPtr;
using Extensions::HttpFilters::AiProtocolManager::AiRequestPropagator;
using Extensions::HttpFilters::AiProtocolManager::AiRequestPtr;
using Extensions::HttpFilters::AiProtocolManager::AiRequestReceiver;
using Extensions::HttpFilters::AiProtocolManager::AiRouteAction;
using Extensions::HttpFilters::AiProtocolManager::LocalReplier;

constexpr absl::string_view BackendKey = "test.ai.backend";

// Names a backend in filter state, as a routing AI filter would, and can ask the AI Protocol
// Manager to pick the cluster again.
class BackendNamingAiFilter : public AiFilter {
public:
  BackendNamingAiFilter(const AiFilterContext& context, bool refresh)
      : context_(context), refresh_(refresh) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());
    context_.stream_info.filterState()->setData(
        BackendKey, std::make_shared<Router::StringAccessorImpl>("cluster_1"),
        StreamInfo::FilterState::LifeSpan::FilterChain);
    if (refresh_) {
      request->requestRouteAction(AiRouteAction::RefreshCluster);
    }
    co_return co_await std::move(propagate_request)(std::move(request));
  }

private:
  const AiFilterContext context_;
  const bool refresh_;
};

// Configured with a Struct whose `refresh` field says whether the filter asks for the refresh.
class BackendNamingAiFilterFactory : public AiFilterConfigFactory {
public:
  absl::StatusOr<AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext&, Stats::Scope&) override {
    const bool refresh =
        dynamic_cast<const Protobuf::Struct&>(config).fields().at("refresh").bool_value();
    return [refresh](const AiFilterContext& context) -> AiFilterSharedPtr {
      return std::make_shared<BackendNamingAiFilter>(context, refresh);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  std::string name() const override { return "test.ai_filters.backend_naming"; }
};

// The route picks cluster_1 when the filter state names it and cluster_0 otherwise.
class AiFilterRouteClusterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  AiFilterRouteClusterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeRoute(bool refresh) {
    setUpstreamCount(2);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters(0));
      cluster->set_name("cluster_1");
      cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
    });
    config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
      auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
          per_route;
      per_route.mutable_request()->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
      std::ignore =
          (*route->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
              .PackFrom(per_route);
      TestUtility::loadFromYaml(R"EOF(
extension:
  name: envoy.router.cluster_specifier_plugin.matcher
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.MatcherClusterSpecifier
    cluster_matcher:
      matcher_tree:
        input:
          name: backend
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput
            key: test.ai.backend
        exact_match_map:
          map:
            cluster_1:
              action:
                name: cluster_1
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction
                  cluster: cluster_1
      on_no_match:
        action:
          name: default
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction
            cluster: cluster_0
)EOF",
                                *route->mutable_route()->mutable_inline_cluster_specifier_plugin());
    });
    config_helper_.prependFilter(fmt::format(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  request_handling: {{}}
  filters:
  - name: test.ai_filters.backend_naming
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        refresh: {}
)EOF",
                                             refresh));
    initialize();
  }

  void expectServedBy(uint64_t upstream_index) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/v1/chat/completions"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"}},
        R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})");
    EXPECT_EQ(waitForNextUpstreamRequest({0, 1}), upstream_index);
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ(response->headers().getStatusValue(), "200");
  }

  BackendNamingAiFilterFactory factory_;
  Registry::InjectFactory<AiFilterConfigFactory> registration_{factory_};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AiFilterRouteClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AiFilterRouteClusterIntegrationTest, RefreshPicksClusterFromAiFilterState) {
  initializeRoute(/*refresh=*/true);
  expectServedBy(1);
}

// The cluster was picked when the headers arrived, before the AI filter wrote the filter state.
TEST_P(AiFilterRouteClusterIntegrationTest, ClusterKeptWhenNotAsked) {
  initializeRoute(/*refresh=*/false);
  expectServedBy(0);
}

} // namespace
} // namespace Envoy
