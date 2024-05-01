#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

const std::string RBAC_CONFIG = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      foo:
        permissions:
          - header:
              name: ":method"
              string_match:
                exact: "GET"
        principals:
          - any: true
)EOF";

const std::string RBAC_CONFIG_WITH_DENY_ACTION = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: DENY
    policies:
      "deny policy":
        permissions:
          - header:
              name: ":method"
              string_match:
                exact: "GET"
        principals:
          - any: true
)EOF";

const std::string RBAC_CONFIG_WITH_PREFIX_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      foo:
        permissions:
          - header:
              name: ":path"
              string_match:
                prefix: "/foo"
        principals:
          - any: true
)EOF";

const std::string RBAC_CONFIG_WITH_PATH_EXACT_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      foo:
        permissions:
          - url_path:
              path: { exact: "/allow" }
        principals:
          - any: true
)EOF";

const std::string RBAC_CONFIG_DENY_WITH_PATH_EXACT_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: DENY
    policies:
      foo:
        permissions:
          - url_path:
              path: { exact: "/deny" }
        principals:
          - any: true
)EOF";

const std::string RBAC_CONFIG_WITH_PATH_IGNORE_CASE_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      foo:
        permissions:
          - url_path:
              path: { exact: "/ignore_case", ignore_case: true }
        principals:
          - any: true
)EOF";

const std::string RBAC_CONFIG_PERMISSION_WITH_URI_PATH_TEMPLATE_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: DENY
    policies:
      foo:
        permissions:
          - uri_template:
              name: envoy.path.match.uri_template.uri_template_matcher
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
                path_template: "/test/deny/path"
        principals:
          - any: true
)EOF";

const std::string RBAC_CONFIG_WITH_LOG_ACTION = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: LOG
    policies:
      foo:
        permissions:
          - header:
              name: ":method"
              string_match:
                exact: "GET"
        principals:
          - any: true
)EOF";

constexpr absl::string_view RBAC_CONFIG_HEADER_MATCH_CONDITION = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      foo:
        permissions:
          - any: true
        principals:
          - any: true
        condition:
          call_expr:
            function: _==_
            args:
            - select_expr:
                operand:
                  select_expr:
                    operand:
                      ident_expr:
                        name: request
                    field: headers
                field: xxx
            - const_expr:
               string_value: {}
)EOF";

const std::string RBAC_MATCHER_CONFIG = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: :method
            value_match:
              exact: GET
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
              name: foo
              action: ALLOW
)EOF";

const std::string RBAC_MATCHER_CONFIG_WITH_DENY_ACTION = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: :method
            value_match:
              exact: GET
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
              name: "deny policy"
              action: DENY
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: none
          action: ALLOW
)EOF";

const std::string RBAC_MATCHER_CONFIG_WITH_PREFIX_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: :path
            value_match:
              prefix: "/foo"
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
              name: foo
              action: ALLOW
)EOF";

// TODO(zhxie): it is not equivalent with the URL path rule in RBAC_CONFIG_WITH_PATH_EXACT_MATCH.
// There will be a replacement when the URL path custom matcher is ready.
const std::string RBAC_MATCHER_CONFIG_WITH_PATH_EXACT_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: :path
            value_match:
              prefix: "/allow"
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
              name: foo
              action: ALLOW
)EOF";

// TODO(zhxie): it is not equivalent with the URL path rule in
// RBAC_CONFIG_DENY_WITH_PATH_EXACT_MATCH. There will be a replacement when the URL path custom
// matcher is ready.
const std::string RBAC_MATCHER_CONFIG_DENY_WITH_PATH_EXACT_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: :path
            value_match:
              prefix: "/deny"
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
              name: foo
              action: DENY
)EOF";

const std::string RBAC_MATCHER_CONFIG_WITH_PATH_IGNORE_CASE_MATCH = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: :path
            value_match:
              exact: "/ignore_case"
              ignore_case: true
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
              name: foo
              action: ALLOW
)EOF";

const std::string RBAC_MATCHER_CONFIG_WITH_LOG_ACTION = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: :method
            value_match:
              exact: GET
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
              name: foo
              action: LOG
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: none
          action: ALLOW
)EOF";

const std::string RBAC_MATCHER_WITH_HTTP_ATTRS_CEL_MATCH_INPUT_CONFIG = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  matcher:
    matcher_list:
      matchers:
        - predicate:
            single_predicate:
              input:
                name: envoy.matching.inputs.cel_data_input
                typed_config:
                  "@type": type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput
              custom_match:
                name: envoy.matching.matchers.cel_matcher
                typed_config:
                  "@type": type.googleapis.com/xds.type.matcher.v3.CelMatcher
                  expr_match:
                    parsed_expr:
                      expr:
                        id: 3
                        call_expr:
                          function: _==_
                          args:
                          - id: 2
                            select_expr:
                              operand:
                                id: 1
                                ident_expr:
                                  name: request
                              field: path
                          - id: 4
                            const_expr:
                              string_value: "/test-localhost-deny"
          on_match:
            matcher:
              matcher_tree:
                input:
                  name: envoy.matching.inputs.source_ip
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
                custom_match:
                  name: envoy.matching.matchers.ip
                  typed_config:
                    "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
                    range_matchers:
                      - ranges:
                          - address_prefix: 127.0.0.1
                        on_match:
                          action:
                            name: envoy.filters.rbac.action
                            typed_config:
                              "@type": type.googleapis.com/envoy.config.rbac.v3.Action
                              name: deny-request
                              action: DENY
              on_no_match:
                action:
                  name: action
                  typed_config:
                    "@type": type.googleapis.com/envoy.config.rbac.v3.Action
                    name: allow-request
                    action: ALLOW
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.rbac.v3.Action
          name: allow-request
          action: ALLOW
)EOF";

using RBACIntegrationTest = HttpProtocolIntegrationTest;

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, RBACIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(RBACIntegrationTest, WithHttpAttributesCelMatchInputDenied) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_MATCHER_WITH_HTTP_ATTRS_CEL_MATCH_INPUT_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The test request utilizing the path '/test-localhost-deny' is expected to be denied by the RBAC
  // filter. This denial is based on the CEL expression that matches the request's path as
  // '/test-localhost-deny' and subsequently restricts all access based on the IP Address, in this
  // instance pointing to localhost.
  auto deny_response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/test-localhost-deny"},
          {":scheme", "http"},
          {":authority", "sni.databricks.com"},
          {"x-forwarded-for", "10.0.0.2"},
      },
      1024);
  ASSERT_TRUE(deny_response->waitForEndStream());
  ASSERT_TRUE(deny_response->complete());
  EXPECT_EQ("403", deny_response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::HasSubstr("rbac_access_denied_matched_policy[deny-request]"));
}

TEST_P(RBACIntegrationTest, WithHttpAttributesCelMatchInputNoMatch) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_MATCHER_WITH_HTTP_ATTRS_CEL_MATCH_INPUT_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The test request utilizing the path '/allow' is expected to be allowed by the RBAC filter as it
  // doesn't match the CEL expression and hit the catch-all path which allows all the traffic.
  auto allow_response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/allow"},
          {":scheme", "http"},
          {":authority", "sni.databricks.com"},
          {"x-forwarded-for", "10.0.0.2"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(allow_response->waitForEndStream());
  ASSERT_TRUE(allow_response->complete());
  EXPECT_EQ("200", allow_response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("via_upstream"));
}

TEST_P(RBACIntegrationTest, Allowed) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("via_upstream"));
}

TEST_P(RBACIntegrationTest, Denied) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::HasSubstr("rbac_access_denied_matched_policy[none]"));
}

TEST_P(RBACIntegrationTest, DeniedWithDenyAction) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_CONFIG_WITH_DENY_ACTION);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  // Note the whitespace in the policy id is replaced by '_'.
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::HasSubstr("rbac_access_denied_matched_policy[deny_policy]"));
}

TEST_P(RBACIntegrationTest, DeniedWithPrefixRule) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) { cfg.mutable_normalize_path()->set_value(false); });
  config_helper_.prependFilter(RBAC_CONFIG_WITH_PREFIX_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/foo/../bar"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, RbacPrefixRuleUseNormalizePath) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) { cfg.mutable_normalize_path()->set_value(true); });
  config_helper_.prependFilter(RBAC_CONFIG_WITH_PREFIX_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/foo/../bar"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, DeniedHeadReply) {
  config_helper_.prependFilter(RBAC_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "HEAD"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().ContentLength());
  EXPECT_NE("0", response->headers().getContentLengthValue());
  EXPECT_THAT(response->body(), ::testing::IsEmpty());
}

TEST_P(RBACIntegrationTest, RouteOverride) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) {
        envoy::extensions::filters::http::rbac::v3::RBACPerRoute per_route_config;
        TestUtility::loadFromJson("{}", per_route_config);

        auto* config = cfg.mutable_route_config()
                           ->mutable_virtual_hosts()
                           ->Mutable(0)
                           ->mutable_typed_per_filter_config();

        (*config)["rbac"].PackFrom(per_route_config);
      });
  config_helper_.prependFilter(RBAC_CONFIG);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, PathWithQueryAndFragmentWithOverride) {
  // Allow client to send path fragment
  disable_client_header_validation_ = true;
  config_helper_.prependFilter(RBAC_CONFIG_WITH_PATH_EXACT_MATCH);
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http_reject_path_with_fragment",
                                    "false");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::vector<std::string> paths{"/allow", "/allow?p1=v1&p2=v2", "/allow?p1=v1#seg"};

  for (const auto& path : paths) {
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{
            {":method", "POST"},
            {":path", path},
            {":scheme", "http"},
            {":authority", "sni.lyft.com"},
            {"x-forwarded-for", "10.0.0.1"},
        },
        1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(RBACIntegrationTest, PathWithFragmentRejectedByDefault) {
  // Prevent UHV in test client from stripping fragment
  disable_client_header_validation_ = true;
  config_helper_.prependFilter(RBAC_CONFIG_WITH_PATH_EXACT_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/allow?p1=v1#seg"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  // Request should not hit the upstream
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// This test ensures that the exact match deny rule is not affected by fragment and query
// when Envoy is configured to strip both fragment and query.
TEST_P(RBACIntegrationTest, DenyExactMatchIgnoresQueryAndFragment) {
  disable_client_header_validation_ = true;
  config_helper_.prependFilter(RBAC_CONFIG_DENY_WITH_PATH_EXACT_MATCH);
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http_reject_path_with_fragment",
                                    "false");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::vector<std::string> paths{"/deny#", "/deny#fragment", "/deny?p1=v1&p2=v2",
                                       "/deny?p1=v1#seg"};

  for (const auto& path : paths) {
    printf("Testing: %s\n", path.c_str());
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{
            {":method", "POST"},
            {":path", path},
            {":scheme", "http"},
            {":authority", "sni.lyft.com"},
            {"x-forwarded-for", "10.0.0.1"},
        },
        1024);

    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
    if (downstreamProtocol() == Http::CodecClient::Type::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
      codec_client_ = makeHttpConnection(lookupPort("http"));
    }
  }
}

TEST_P(RBACIntegrationTest, PathIgnoreCase) {
  config_helper_.prependFilter(RBAC_CONFIG_WITH_PATH_IGNORE_CASE_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::vector<std::string> paths{"/ignore_case", "/IGNORE_CASE", "/ignore_CASE"};

  for (const auto& path : paths) {
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{
            {":method", "POST"},
            {":path", path},
            {":scheme", "http"},
            {":authority", "sni.lyft.com"},
            {"x-forwarded-for", "10.0.0.1"},
        },
        1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(RBACIntegrationTest, PermissionUriPathTemplateMatch) {
  config_helper_.prependFilter(RBAC_CONFIG_PERMISSION_WITH_URI_PATH_TEMPLATE_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/test/deny/path"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, LogConnectionAllow) {
  config_helper_.prependFilter(RBAC_CONFIG_WITH_LOG_ACTION);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Basic CEL match on a header value.
TEST_P(RBACIntegrationTest, HeaderMatchCondition) {
  config_helper_.prependFilter(fmt::format(RBAC_CONFIG_HEADER_MATCH_CONDITION, "yyy"));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/path"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"xxx", "yyy"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// CEL match on a header value in which the header is a duplicate. Verifies we handle string
// copying correctly inside the CEL expression.
TEST_P(RBACIntegrationTest, HeaderMatchConditionDuplicateHeaderNoMatch) {
  config_helper_.prependFilter(fmt::format(RBAC_CONFIG_HEADER_MATCH_CONDITION, "yyy"));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/path"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"xxx", "yyy"},
          {"xxx", "zzz"},
      },
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// CEL match on a header value in which the header is a duplicate. Verifies we handle string
// copying correctly inside the CEL expression.
TEST_P(RBACIntegrationTest, HeaderMatchConditionDuplicateHeaderMatch) {
  config_helper_.prependFilter(fmt::format(RBAC_CONFIG_HEADER_MATCH_CONDITION, "yyy,zzz"));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/path"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"xxx", "yyy"},
          {"xxx", "zzz"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, MatcherAllowed) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("via_upstream"));
}

TEST_P(RBACIntegrationTest, MatcherDenied) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::HasSubstr("rbac_access_denied_matched_policy[none]"));
}

TEST_P(RBACIntegrationTest, MatcherDeniedWithDenyAction) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_WITH_DENY_ACTION);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  // Note the whitespace in the policy id is replaced by '_'.
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              testing::HasSubstr("rbac_access_denied_matched_policy[deny_policy]"));
}

TEST_P(RBACIntegrationTest, MatcherDeniedWithPrefixRule) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) { cfg.mutable_normalize_path()->set_value(false); });
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_WITH_PREFIX_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/foo/../bar"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, RbacPrefixRuleUseNormalizePathMatcher) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) { cfg.mutable_normalize_path()->set_value(true); });
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_WITH_PREFIX_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/foo/../bar"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, MatcherDeniedHeadReply) {
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "HEAD"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().ContentLength());
  EXPECT_NE("0", response->headers().getContentLengthValue());
  EXPECT_THAT(response->body(), ::testing::IsEmpty());
}

TEST_P(RBACIntegrationTest, MatcherRouteOverride) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) {
        envoy::extensions::filters::http::rbac::v3::RBACPerRoute per_route_config;
        TestUtility::loadFromJson("{}", per_route_config);

        auto* config = cfg.mutable_route_config()
                           ->mutable_virtual_hosts()
                           ->Mutable(0)
                           ->mutable_typed_per_filter_config();

        (*config)["rbac"].PackFrom(per_route_config);
      });
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(RBACIntegrationTest, PathMatcherWithQueryAndFragmentWithOverride) {
  // Allow client to send path fragment
  disable_client_header_validation_ = true;
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_WITH_PATH_EXACT_MATCH);
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http_reject_path_with_fragment",
                                    "false");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::vector<std::string> paths{"/allow", "/allow?p1=v1&p2=v2", "/allow?p1=v1#seg"};

  for (const auto& path : paths) {
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{
            {":method", "POST"},
            {":path", path},
            {":scheme", "http"},
            {":authority", "sni.lyft.com"},
            {"x-forwarded-for", "10.0.0.1"},
        },
        1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(RBACIntegrationTest, PathMatcherWithFragmentRejectedByDefault) {
  // Allow client to send path fragment
  disable_client_header_validation_ = true;
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_WITH_PATH_EXACT_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/allow?p1=v1#seg"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  // Request should not hit the upstream
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// This test ensures that the exact match deny rule is not affected by fragment and query
// when Envoy is configured to strip both fragment and query.
TEST_P(RBACIntegrationTest, MatcherDenyExactMatchIgnoresQueryAndFragment) {
  disable_client_header_validation_ = true;
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_DENY_WITH_PATH_EXACT_MATCH);
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http_reject_path_with_fragment",
                                    "false");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::vector<std::string> paths{"/deny#", "/deny#fragment", "/deny?p1=v1&p2=v2",
                                       "/deny?p1=v1#seg"};

  for (const auto& path : paths) {
    printf("Testing: %s\n", path.c_str());
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{
            {":method", "POST"},
            {":path", path},
            {":scheme", "http"},
            {":authority", "sni.lyft.com"},
            {"x-forwarded-for", "10.0.0.1"},
        },
        1024);

    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("403", response->headers().getStatusValue());
    if (downstreamProtocol() == Http::CodecClient::Type::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
      codec_client_ = makeHttpConnection(lookupPort("http"));
    }
  }
}

TEST_P(RBACIntegrationTest, PathIgnoreCaseMatcher) {
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_WITH_PATH_IGNORE_CASE_MATCH);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::vector<std::string> paths{"/ignore_case", "/IGNORE_CASE", "/ignore_CASE"};

  for (const auto& path : paths) {
    auto response = codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{
            {":method", "POST"},
            {":path", path},
            {":scheme", "http"},
            {":authority", "sni.lyft.com"},
            {"x-forwarded-for", "10.0.0.1"},
        },
        1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(RBACIntegrationTest, MatcherLogConnectionAllow) {
  config_helper_.prependFilter(RBAC_MATCHER_CONFIG_WITH_LOG_ACTION);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "sni.lyft.com"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Helper for integration testing of RBAC filter with dynamic forward proxy.
class RbacDynamicForwardProxyIntegrationHelper
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public Event::TestUsingSimulatedTime,
      public HttpIntegrationTest {
public:
  RbacDynamicForwardProxyIntegrationHelper()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithFilterConfigs(bool save_filter_state, const std::string& rbac_config) {
    setUpstreamProtocol(Http::CodecType::HTTP1);

    const std::string save_upstream_config =
        save_filter_state ? "save_upstream_address: true " : "";
    const std::string dfp_config =
        fmt::format(R"EOF(
name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  {}
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
)EOF",
                    save_upstream_config, Network::Test::ipVersionToDnsFamily(GetParam()));

    config_helper_.prependFilter(rbac_config);

    config_helper_.prependFilter(dfp_config);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      bootstrap.mutable_dynamic_resources()
          ->mutable_cds_config()
          ->mutable_path_config_source()
          ->set_path(cds_helper_.cdsPath());
      bootstrap.mutable_static_resources()->clear_clusters();
    });

    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

    // Setup the initial CDS cluster.
    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    cluster_.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
    protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
    protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
    ConfigHelper::setProtocolOptions(cluster_, protocol_options);

    const std::string cluster_type_config = fmt::format(
        R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
)EOF",
        Network::Test::ipVersionToDnsFamily(GetParam()));

    TestUtility::loadFromYaml(cluster_type_config, *cluster_.mutable_cluster_type());
    // Load the CDS cluster and wait for it to initialize.
    cds_helper_.setCds({cluster_});
    HttpIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
  bool write_cache_file_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RbacDynamicForwardProxyIntegrationHelper,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that if upstream ip matcher is configured, upstream address is saved by a filter(dynamic
// forward proxy in this case). If not saved, the request would be denied.
TEST_P(RbacDynamicForwardProxyIntegrationHelper, AllowIpWithNoFilterState) {
  const std::string rbac_config = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      foo:
        permissions:
        - or_rules:
            rules:
            - matcher:
                name: envoy.rbac.matchers.upstream_ip_port
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.rbac.matchers.upstream_ip_port.v3.UpstreamIpPortMatcher
                  upstream_ip:
                    address_prefix: 127.0.0.1
                    prefix_len: 24
        principals:
          - any: true
)EOF";

  initializeWithFilterConfigs(false, rbac_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Verify that if upstream ip matcher is configured and upstream address is saved by dynamic
// forward proxy, then RBAC policy is evaluated correctly for `or_rules`.
#ifndef WIN32
// TODO(conqerAtapple) figure out why this test doesn't pass on windows.
TEST_P(RbacDynamicForwardProxyIntegrationHelper, DenyIpOrPortWithFilterState) {
  const std::string rbac_config = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: DENY
    policies:
      foo:
        permissions:
        - or_rules:
            rules:
            - matcher:
                name: envoy.rbac.matchers.upstream_ip_port
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.rbac.matchers.upstream_ip_port.v3.UpstreamIpPortMatcher
                  upstream_ip:
                    address_prefix: 127.2.1.1
                    prefix_len: 24
            - matcher:
                name: envoy.rbac.matchers.upstream_ip_port
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.rbac.matchers.upstream_ip_port.v3.UpstreamIpPortMatcher
                  upstream_ip:
                    address_prefix: 127.0.0.1
                    prefix_len: 24
            - matcher:
                name: envoy.rbac.matchers.upstream_ip_port
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.rbac.matchers.upstream_ip_port.v3.UpstreamIpPortMatcher
                  upstream_ip:
                    address_prefix: ::1
                    prefix_len: 24
        principals:
          - any: true
)EOF";

  initializeWithFilterConfigs(true, rbac_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}
#endif

} // namespace
} // namespace Envoy
