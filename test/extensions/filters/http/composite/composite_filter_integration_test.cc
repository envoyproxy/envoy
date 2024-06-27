#include <optional>
#include <string>

#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/address.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/http/matching/inputs.h"
#include "source/extensions/filters/http/match_delegate/config.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/integration/filters/add_body_filter.pb.h"
#include "test/integration/filters/server_factory_context_filter_config.pb.h"
#include "test/integration/filters/set_response_code_filter_config.pb.h"
#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {

namespace {

using envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute;
using envoy::extensions::filters::http::composite::v3::ExecuteFilterAction;
using envoy::type::matcher::v3::HttpRequestHeaderMatchInput;
using test::integration::filters::SetResponseCodeFilterConfig;
using test::integration::filters::SetResponseCodeFilterConfigDual;
using test::integration::filters::SetResponseCodePerRouteFilterConfig;
using test::integration::filters::SetResponseCodePerRouteFilterConfigDual;
using xds::type::matcher::v3::Matcher_OnMatch;

struct CompositeFilterTestParams {
  Network::Address::IpVersion version;
  bool is_downstream;
  bool is_dual_factory;
};

class CompositeFilterIntegrationTest : public testing::TestWithParam<CompositeFilterTestParams>,
                                       public HttpIntegrationTest {
public:
  CompositeFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().version),
        downstream_filter_(GetParam().is_downstream), is_dual_factory_(GetParam().is_dual_factory),
        proto_type_(is_dual_factory_ ? proto_type_dual_ : proto_type_downstream_) {}

  ExtensionWithMatcherPerRoute createPerRouteConfig(
      std::function<void(envoy::config::core::v3::TypedExtensionConfig*)> base_action_function) {

    ExtensionWithMatcherPerRoute per_route_config;
    auto matcher_tree = per_route_config.mutable_xds_matcher()->mutable_matcher_tree();

    auto matcher_input = matcher_tree->mutable_input();
    matcher_input->set_name("request-headers");
    HttpRequestHeaderMatchInput match_input;
    match_input.set_header_name("match-header");
    matcher_input->mutable_typed_config()->PackFrom(match_input);

    auto map = matcher_tree->mutable_exact_match_map()->mutable_map();
    Matcher_OnMatch match;
    auto mutable_action = match.mutable_action();
    mutable_action->set_name("composite-action");

    ExecuteFilterAction filter_action;
    base_action_function(filter_action.mutable_typed_config());
    mutable_action->mutable_typed_config()->PackFrom(filter_action);

    (*map)["match"] = match;
    return per_route_config;
  }

  template <class ProtoConfig>
  void addPerRouteResponseCodeFilterTyped(const std::string& filter_name,
                                          const std::string& route_prefix, const int& code,
                                          bool response_prefix = false) {
    ProtoConfig set_response_code;
    set_response_code.set_code(code);
    if (response_prefix) {
      set_response_code.set_prefix("skipLocalReplyAndContinue");
    }
    auto per_route_config = createPerRouteConfig([set_response_code](auto* cfg) {
      cfg->set_name("set-response-code-filter");
      cfg->mutable_typed_config()->PackFrom(set_response_code);
    });
    config_helper_.addConfigModifier(
        [per_route_config, filter_name, route_prefix](ConfigHelper::HttpConnectionManager& cm) {
          auto* vh = cm.mutable_route_config()->mutable_virtual_hosts(0);
          auto* route = vh->mutable_routes()->Mutable(0);
          route->mutable_match()->set_prefix(route_prefix);
          route->mutable_route()->set_cluster("cluster_0");
          (*route->mutable_typed_per_filter_config())[filter_name].PackFrom(per_route_config);
        });
  }

  void addPerRouteResponseCodeFilter(const std::string& filter_name,
                                     const std::string& route_prefix, const int& code,
                                     bool response_prefix = false) {
    if (is_dual_factory_) {
      addPerRouteResponseCodeFilterTyped<SetResponseCodeFilterConfigDual>(filter_name, route_prefix,
                                                                          code, response_prefix);
    } else {
      addPerRouteResponseCodeFilterTyped<SetResponseCodeFilterConfig>(filter_name, route_prefix,
                                                                      code, response_prefix);
    }
  }

  template <class PerRouteProtoConfig>
  void addResponseCodeFilterPerRouteConfigTyped(const std::string& filter_name,
                                                const std::string& route_prefix, const int& code,
                                                bool response_prefix = false) {
    PerRouteProtoConfig set_response_code_per_route_config;
    set_response_code_per_route_config.set_code(code);
    if (response_prefix) {
      set_response_code_per_route_config.set_prefix("skipLocalReplyAndContinue");
    }
    config_helper_.addConfigModifier([set_response_code_per_route_config, filter_name,
                                      route_prefix](ConfigHelper::HttpConnectionManager& cm) {
      auto* vh = cm.mutable_route_config()->mutable_virtual_hosts(0);
      auto* route = vh->mutable_routes()->Mutable(0);
      route->mutable_match()->set_prefix(route_prefix);
      route->mutable_route()->set_cluster("cluster_0");
      (*route->mutable_typed_per_filter_config())[filter_name].PackFrom(
          set_response_code_per_route_config);
    });
  }

  void addResponseCodeFilterPerRouteConfig(const std::string& filter_name,
                                           const std::string& route_prefix, const int& code,
                                           bool response_prefix = false) {
    if (is_dual_factory_) {
      addResponseCodeFilterPerRouteConfigTyped<SetResponseCodePerRouteFilterConfigDual>(
          filter_name, route_prefix, code, response_prefix);
    } else {
      addResponseCodeFilterPerRouteConfigTyped<SetResponseCodePerRouteFilterConfig>(
          filter_name, route_prefix, code, response_prefix);
    }
  }

  void prependCompositeFilter(const std::string& name = "composite") {
    config_helper_.prependFilter(absl::StrFormat(R"EOF(
      name: %s
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
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
                    name: composite-action
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
                      typed_config:
                        name: set-response-code
                        typed_config:
                          "@type": type.googleapis.com/test.integration.filters.%s
                          code: 403
    )EOF",
                                                 name, proto_type_),
                                 downstream_filter_);
  }

  void prependCompositeDynamicFilter(const std::string& name = "composite",
                                     const std::string& path = "set_response_code.yaml",
                                     bool sampling = true) {
    int numerator = 100;
    if (!sampling) {
      numerator = 0;
    }
    config_helper_.prependFilter(
        TestEnvironment::substitute(absl::StrFormat(R"EOF(
      name: %s
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
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
                    name: composite-action
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
                        sample_percent:
                          default_value:
                            numerator: %d
                            denominator: HUNDRED
                        dynamic_config:
                          name: set-response-code
                          config_discovery:
                            config_source:
                                path_config_source:
                                  path: "{{ test_tmpdir }}/%s"
                            type_urls:
                            - type.googleapis.com/test.integration.filters.%s
    )EOF",
                                                    name, numerator, path, proto_type_)),
        downstream_filter_);

    TestEnvironment::writeStringToFileForTest("set_response_code.yaml",
                                              absl::StrFormat(R"EOF(
resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: set-response-code
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.%s
      code: 403)EOF",
                                                              proto_type_),
                                              false);
  }

  void prependMissingCompositeDynamicFilter(const std::string& name = "composite") {
    config_helper_.prependFilter(TestEnvironment::substitute(absl::StrFormat(R"EOF(
      name: %s
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
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
                    name: composite-action
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
                        dynamic_config:
                          name: missing-config
                          config_discovery:
                            config_source:
                                api_config_source:
                                  api_type: GRPC
                                  grpc_services:
                                    envoy_grpc:
                                      cluster_name: "ecds_cluster"
                            type_urls:
                            - type.googleapis.com/test.integration.filters.%s
    )EOF",
                                                                             name, proto_type_)),
                                 downstream_filter_);
  }

  const Http::TestRequestHeaderMapImpl match_request_headers_ = {{":method", "GET"},
                                                                 {":path", "/somepath"},
                                                                 {":scheme", "http"},
                                                                 {"match-header", "match"},
                                                                 {":authority", "blah"}};

  void initialize() override {
    if (!downstream_filter_) {
      setUpstreamProtocol(Http::CodecType::HTTP2);
    }
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ecds_cluster->set_name("ecds_cluster");
      ecds_cluster->mutable_load_assignment()->set_cluster_name("ecds_cluster");
    });
    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  static std::vector<CompositeFilterTestParams> getValuesForCompositeFilterTest() {
    std::vector<CompositeFilterTestParams> ret;
    for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
      for (bool is_downstream : {true, false}) {
        for (bool is_dual_factory : {true, false}) {
          if (!is_downstream && !is_dual_factory) {
            // Skip upstream filter test for non-dual filter.
            continue;
          }
          CompositeFilterTestParams params;
          params.version = ip_version;
          params.is_downstream = is_downstream;
          params.is_dual_factory = is_dual_factory;
          ret.push_back(params);
        }
      }
    }
    return ret;
  }

  static std::string CompositeFilterTestParamsToString(
      const ::testing::TestParamInfo<CompositeFilterTestParams>& params) {
    return absl::StrCat(
        (params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
        (params.param.is_downstream ? "Downstream_" : "Upstream_"),
        (params.param.is_dual_factory ? "DualFactory" : "DownstreamFactory"));
  }

  const std::string proto_type_dual_ = "SetResponseCodeFilterConfigDual";
  const std::string proto_type_downstream_ = "SetResponseCodeFilterConfig";
  bool downstream_filter_{true};
  bool is_dual_factory_{false};
  std::string proto_type_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, CompositeFilterIntegrationTest,
    testing::ValuesIn(CompositeFilterIntegrationTest::getValuesForCompositeFilterTest()),
    CompositeFilterIntegrationTest::CompositeFilterTestParamsToString);

// Verifies that if we don't match the match action the request is proxied as normal, while if the
// match action is hit we apply the specified filter to the stream.
TEST_P(CompositeFilterIntegrationTest, TestBasic) {
  prependCompositeFilter();
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  {
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  }

  {
    auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("403"));
  }
}

// Verifies that if we don't match the match action the request is proxied as normal, while if the
// match action is hit we apply the specified dynamic filter to the stream.
TEST_P(CompositeFilterIntegrationTest, TestBasicDynamicFilter) {
  prependCompositeDynamicFilter("composite-dynamic");
  initialize();
  if (downstream_filter_) {
    test_server_->waitForCounterGe(
        "extension_config_discovery.http_filter.set-response-code.config_reload", 1);
  } else {
    test_server_->waitForCounterGe(
        "extension_config_discovery.upstream_http_filter.set-response-code.config_reload", 1);
  }
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  {
    // Sending default headers, not matching the xDS config, the request reaches backend.
    // 200 is sent back.
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  }

  {
    // Sending headers matching the xDS config, sampled and the action filter is
    // executed. Local reply with status code 403 is sent back.
    auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("403"));
  }
}

// Verifies that with dynamic config, if not sampled, then the action filter is skipped.
TEST_P(CompositeFilterIntegrationTest, TestBasicDynamicFilterNoSampling) {
  prependCompositeDynamicFilter("composite-dynamic", "set_response_code.yaml", false);
  initialize();
  if (downstream_filter_) {
    test_server_->waitForCounterGe(
        "extension_config_discovery.http_filter.set-response-code.config_reload", 1);
  } else {
    test_server_->waitForCounterGe(
        "extension_config_discovery.upstream_http_filter.set-response-code.config_reload", 1);
  }
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  {
    // Sending default headers, not matching the xDS config, the request reaches backend,
    // 200 is sent back.
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  }

  {
    // Sending headers matching the xDS config, not sampled. The action filter is
    // skipped. The request reaches backend, and 200 is sent back.
    // 200 is returned back.
    auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  }
}

// Verifies that if ECDS response is not sent, the missing filter config is applied that returns
// 500.
TEST_P(CompositeFilterIntegrationTest, TestMissingDynamicFilter) {
  prependMissingCompositeDynamicFilter("composite-dynamic-missing");
  initialize();
  if (downstream_filter_) {
    test_server_->waitForCounterGe(
        "extension_config_discovery.http_filter.missing-config.config_fail", 1);
  } else {
    test_server_->waitForCounterGe(
        "extension_config_discovery.upstream_http_filter.missing-config.config_fail", 1);
  }
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("500"));
}

// Verifies function of the per-route config in the ExtensionWithMatcher class.
TEST_P(CompositeFilterIntegrationTest, TestPerRoute) {
  // Per-route configuration only applies to downstream filter.
  if (!downstream_filter_) {
    return;
  }
  prependCompositeFilter();
  addPerRouteResponseCodeFilter(/*filter_name=*/"composite", /*route_prefix=*/"/somepath",
                                /*code=*/401);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("401"));
}

// Verifies set_response_code filter's per-route config overrides the filter config.
TEST_P(CompositeFilterIntegrationTest, TestPerRouteResponseCodeConfig) {
  // Per-route configuration only applies to downstream filter.
  if (!downstream_filter_) {
    return;
  }
  std::string top_level_filter_name = "match_delegate_filter";
  prependCompositeFilter(top_level_filter_name);

  addResponseCodeFilterPerRouteConfig(/*filter_name=*/top_level_filter_name,
                                      /*route_prefix=*/"/somepath",
                                      /*code=*/406);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  // Verifies that 406 from per route config is used, rather than 403 from filter config.
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("406"));
}

// Test an empty match tree resolving with a per route config.
TEST_P(CompositeFilterIntegrationTest, TestPerRouteEmptyMatcher) {
  // Per-route configuration only applies to downstream filter.
  if (!downstream_filter_) {
    return;
  }
  config_helper_.prependFilter(R"EOF(
      name: composite
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
    )EOF",
                               downstream_filter_);
  addPerRouteResponseCodeFilter(/*filter_name=*/"composite", /*route_prefix=*/"/somepath",
                                /*code=*/402);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("402"));
}

// Test that the specified filters apply per route configs to requests.
TEST_P(CompositeFilterIntegrationTest, TestPerRouteEmptyMatcherMultipleFilters) {
  // Per-route configuration only applies to downstream filter.
  if (!downstream_filter_) {
    return;
  }
  config_helper_.prependFilter(R"EOF(
      name: composite_2
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
    )EOF",
                               downstream_filter_);
  config_helper_.prependFilter(R"EOF(
      name: composite
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
    )EOF",
                               downstream_filter_);

  addPerRouteResponseCodeFilter(/*filter_name=*/"composite", /*route_prefix=*/"/somepath",
                                /*code=*/407, /*response_prefix=*/true);
  addPerRouteResponseCodeFilter(/*filter_name=*/"composite_2", /*route_prefix=*/"/somepath",
                                /*code=*/402);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(match_request_headers_, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("402"));
}

class CompositeFilterSeverContextIntegrationTest
    : public HttpIntegrationTest,
      public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
public:
  CompositeFilterSeverContextIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create separate "upstreams" for test gRPC servers
    for (int i = 0; i < 2; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void TearDown() override {
    if (connection_) {
      ASSERT_TRUE(connection_->close());
      ASSERT_TRUE(connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig(bool downstream_filter, bool downstream, int sample_percent) {
    config_helper_.addConfigModifier([downstream_filter, downstream, sample_percent,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

      // Loading filter config from YAML for cluster_0.
      prependServerFactoryContextFilter(downstream_filter, downstream, sample_percent);
      // Clusters for test gRPC servers. The HTTP filters is not needed for these clusters.
      for (size_t i = 0; i < grpc_upstreams_.size(); ++i) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        std::string cluster_name = absl::StrCat("test_server_", i);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
        auto* address = server_cluster->mutable_load_assignment()
                            ->add_endpoints()
                            ->add_lb_endpoints()
                            ->mutable_endpoint()
                            ->mutable_address()
                            ->mutable_socket_address();
        address->set_address(Network::Test::getLoopbackAddressString(ipVersion()));
        envoy::extensions::upstreams::http::v3::HttpProtocolOptions protocol_options;
        protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
        (*server_cluster->mutable_typed_extension_protocol_options())
            ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                .PackFrom(protocol_options);
      }
      // Parameterize with defer processing to prevent bit rot as filter made
      // assumptions of data flow, prior relying on eager processing.
      config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                        deferredProcessing() ? "true" : "false");
    });

    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  void
  prependServerFactoryContextFilter(bool downstream_filter, bool downstream, int sample_percent,
                                    const std::string& name = "envoy.filters.http.match_delegate") {
    std::string context_filter_config = "ServerFactoryContextFilterConfig";
    if (!downstream_filter) {
      context_filter_config = "ServerFactoryContextFilterConfigDual";
    }

    std::string address_prefix = "";
    if (ipVersion() == Network::Address::IpVersion::v6) {
      address_prefix = "ipv6:///";
    }

    std::string grpc_config;
    if (clientType() == Grpc::ClientType::EnvoyGrpc) {
      grpc_config = absl::StrFormat(R"EOF(
                            envoy_grpc:
                              cluster_name: test_server_0

)EOF");
    } else {
      grpc_config = absl::StrFormat(R"EOF(
                            google_grpc:
                              target_uri: %s%s
                              stat_prefix: test_server_0
)EOF",
                                    address_prefix, grpc_upstreams_[0]->localAddress()->asString());
    }

    config_helper_.prependFilter(absl::StrFormat(R"EOF(
      name: %s
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
        extension_config:
          name: composite
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
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
                    name: composite-action
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
                      sample_percent:
                        default_value:
                          numerator: %d
                          denominator: HUNDRED
                      typed_config:
                        name: server-factory-context-filter
                        typed_config:
                          "@type": type.googleapis.com/test.integration.filters.%s
                          grpc_service:
                            %s
    )EOF",
                                                 name, sample_percent, context_filter_config,
                                                 grpc_config),
                                 downstream);
  }

  void serverContextBasicFlowTest(int sample_percent) {
    HttpIntegrationTest::initialize();

    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    const Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                            {":path", "/somepath"},
                                                            {":scheme", "http"},
                                                            {"match-header", "match"},
                                                            {":authority", "blah"}};
    // Send request from downstream to upstream.
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    // Send request to side stream server when all sampled, i.e, sample_percent = 100.
    if (sample_percent == 100) {
      // Wait for side stream request.
      helloworld::HelloRequest request;
      request.set_name("hello");
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, connection_));
      ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream_));
      ASSERT_TRUE(stream_->waitForGrpcMessage(*dispatcher_, request));

      // Start the grpc side stream.
      stream_->startGrpcStream();

      // Send the side stream response.
      helloworld::HelloReply reply;
      reply.set_message("ack");
      stream_->sendGrpcMessage(reply);
    }

    // Handle the upstream request.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    if (sample_percent == 100) {
      // Close the grpc stream.
      stream_->finishGrpcStream(Grpc::Status::Ok);
    }

    // Verify the response from upstream to downstream.
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ(response->headers().getStatusValue(), "200");
  }

  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr connection_;
  FakeStreamPtr stream_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, CompositeFilterSeverContextIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

TEST_P(CompositeFilterSeverContextIntegrationTest,
       BasicFlowDownstreamFilterInDownstreamAllSampled) {
  initializeConfig(/*downstream_filter*/ true, /*downstream*/ true, /*sample_percent*/ 100);
  serverContextBasicFlowTest(100);
}

TEST_P(CompositeFilterSeverContextIntegrationTest, BasicFlowDualFilterInDownstreamAllSampled) {
  initializeConfig(/*downstream_filter*/ false, /*downstream*/ true, /*sample_percent*/ 100);
  serverContextBasicFlowTest(100);
}

TEST_P(CompositeFilterSeverContextIntegrationTest, BasicFlowDualFilterInUpstreamAllSampled) {
  initializeConfig(/*downstream_filter*/ false, /*downstream*/ false, /*sample_percent*/ 100);
  serverContextBasicFlowTest(100);
}

TEST_P(CompositeFilterSeverContextIntegrationTest,
       BasicFlowDownstreamFilterInDownstreamNoneSampled) {
  initializeConfig(/*downstream_filter*/ true, /*downstream*/ true, /*sample_percent*/ 0);
  serverContextBasicFlowTest(0);
}

TEST_P(CompositeFilterSeverContextIntegrationTest, BasicFlowDualFilterInDownstreamNoneSampled) {
  initializeConfig(/*downstream_filter*/ false, /*downstream*/ true, /*sample_percent*/ 0);
  serverContextBasicFlowTest(0);
}

TEST_P(CompositeFilterSeverContextIntegrationTest, BasicFlowDualFilterInUpstreamNoneSampled) {
  initializeConfig(/*downstream_filter*/ false, /*downstream*/ false, /*sample_percent*/ 0);
  serverContextBasicFlowTest(0);
}

} // namespace
} // namespace Envoy
