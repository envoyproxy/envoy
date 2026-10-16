#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"

#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace {

using Extensions::HttpFilters::AiProtocolManager::AiFilter;
using Extensions::HttpFilters::AiProtocolManager::AiFilterCallbacks;
using Extensions::HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using Extensions::HttpFilters::AiProtocolManager::AiFilterContext;
using Extensions::HttpFilters::AiProtocolManager::AiFilterFactoryCb;
using Extensions::HttpFilters::AiProtocolManager::AiFilterSharedPtr;
using Extensions::HttpFilters::AiProtocolManager::AiRequestPropagator;
using Extensions::HttpFilters::AiProtocolManager::AiRequestPtr;
using Extensions::HttpFilters::AiProtocolManager::AiRequestReceiver;
using Extensions::HttpFilters::AiProtocolManager::LocalReplier;
using Extensions::HttpFilters::AiProtocolManager::UpstreamCandidate;
using Extensions::HttpFilters::AiProtocolManager::UpstreamCandidatesEditor;
using Extensions::HttpFilters::AiProtocolManager::UpstreamCandidatesPolicy;

// Seeds its configured candidates, as a routing AI filter would.
class SeedingAiFilter : public AiFilter {
public:
  explicit SeedingAiFilter(std::vector<UpstreamCandidate> candidates)
      : candidates_(std::move(candidates)) {}

  void setAiFilterCallbacks(std::shared_ptr<AiFilterCallbacks> callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());
    if (OptRef<UpstreamCandidatesEditor> editor = callbacks_->upstreamCandidates();
        editor.has_value() && !candidates_.empty()) {
      CO_RETURN_IF_ERROR(editor->seed(candidates_, "test"));
    }
    co_return co_await std::move(propagate_request)(std::move(request));
  }

private:
  const std::vector<UpstreamCandidate> candidates_;
  std::shared_ptr<AiFilterCallbacks> callbacks_;
};

// Configured with a Struct listing `candidates`, each with a name, a cluster and maybe a model.
class SeedingAiFilterFactory : public AiFilterConfigFactory {
public:
  absl::StatusOr<AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext&, Stats::Scope&) override {
    std::vector<UpstreamCandidate> candidates;
    for (const Protobuf::Value& value : dynamic_cast<const Protobuf::Struct&>(config)
                                            .fields()
                                            .at("candidates")
                                            .list_value()
                                            .values()) {
      const auto& fields = value.struct_value().fields();
      const auto model = fields.find("model");
      candidates.push_back({fields.at("name").string_value(), fields.at("cluster").string_value(),
                            model != fields.end() ? model->second.string_value() : "", ""});
    }
    return [candidates](const AiFilterContext&) -> AiFilterSharedPtr {
      return std::make_shared<SeedingAiFilter>(candidates);
    };
  }
  UpstreamCandidatesPolicy upstreamCandidatesPolicy(const Protobuf::Message&) const override {
    return {/*seed=*/true};
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  std::string name() const override { return "test.ai_filters.seeding"; }
};

// The route sends requests to cluster_0 unless the AI Protocol Manager commits a candidate.
class AiFilterRouteClusterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  AiFilterRouteClusterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeRoute(absl::string_view candidates, bool priority_group = true) {
    setUpstreamCount(2);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters(0));
      cluster->set_name("cluster_1");
      cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
    });
    config_helper_.addConfigModifier([priority_group](ConfigHelper::HttpConnectionManager& hcm) {
      auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
          per_route;
      per_route.mutable_request()->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
      std::ignore =
          (*route->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
              .PackFrom(per_route);
      if (!priority_group) {
        return;
      }
      TestUtility::loadFromYaml(R"EOF(
extension:
  name: envoy.router.cluster_specifier_plugin.priority_group
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier
    priority_groups:
    - name: default
      clusters:
      - cluster_name: cluster_0
        weight: 1
    override_metadata_namespace: envoy.ai.upstream.candidates
)EOF",
                                *route->mutable_route()->mutable_inline_cluster_specifier_plugin());
    });
    config_helper_.prependFilter(fmt::format(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  request_handling: {{}}
  filters:
  - name: seeding
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        candidates: {}
)EOF",
                                             candidates));
    useAccessLog("%RESPONSE_CODE_DETAILS%");
    initialize();
  }

  IntegrationStreamDecoderPtr sendRequest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    return codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/v1/chat/completions"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-type", "application/json"}},
        R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})");
  }

  // Returns the model the upstream received.
  std::string expectServedBy(uint64_t upstream_index) {
    auto response = sendRequest();
    EXPECT_EQ(waitForNextUpstreamRequest({0, 1}), upstream_index);
    const std::string model =
        nlohmann::json::parse(upstream_request_->body().toString())["model"].get<std::string>();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ(response->headers().getStatusValue(), "200");
    return model;
  }

  SeedingAiFilterFactory factory_;
  Registry::InjectFactory<AiFilterConfigFactory> registration_{factory_};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AiFilterRouteClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AiFilterRouteClusterIntegrationTest, CommittedCandidateRoutesAndRewrites) {
  initializeRoute(R"([{name: one, cluster: cluster_1, model: gpt-4o}])");
  EXPECT_EQ(expectServedBy(1), "gpt-4o");
  test_server_->waitForCounter("ai_protocol_manager.upstream_candidates_committed", testing::Eq(1));
}

TEST_P(AiFilterRouteClusterIntegrationTest, NoSeedKeepsTheConfiguredGroup) {
  initializeRoute("[]");
  EXPECT_EQ(expectServedBy(0), "gpt-4");
}

TEST_P(AiFilterRouteClusterIntegrationTest, NoEligibleCandidateIsRejected) {
  initializeRoute(R"([{name: one, cluster: missing}])");
  auto response = sendRequest();
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "503");
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::HasSubstr("ai_protocol_manager_upstream_candidates_exhausted"));
}

// A plain cluster route does not follow the candidates, so the request goes out unchanged.
TEST_P(AiFilterRouteClusterIntegrationTest, PlainRouteDoesNotFollow) {
  initializeRoute(R"([{name: one, cluster: cluster_1, model: gpt-4o}])",
                  /*priority_group=*/false);
  EXPECT_EQ(expectServedBy(0), "gpt-4");
  test_server_->waitForCounter("ai_protocol_manager.upstream_candidates_not_followed",
                               testing::Eq(1));
}

} // namespace
} // namespace Envoy
