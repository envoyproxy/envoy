#include <chrono>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/address_impl.h"
#include "common/router/config_impl.h"
#include "common/stream_info/filter_state_impl.h"

#include "test/common/router/route_fuzz.pb.h"
#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/fuzz/utility.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ContainerEq;
using testing::Eq;
using testing::Matcher;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

// Wrap ConfigImpl, the target of tests to allow us to regenerate the route_fuzz_test
// corpus when run with:
//   bazel run //test/common/router:config_impl_test
//     --test_env="ROUTE_CORPUS_PATH=$PWD/test/common/router/route_corpus"
class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(const envoy::config::route::v3::RouteConfiguration& config,
                 Server::Configuration::ServerFactoryContext& factory_context,
                 bool validate_clusters_default)
      : ConfigImpl(config, factory_context, ProtobufMessage::getNullValidationVisitor(),
                   validate_clusters_default),
        config_(config) {}

  void setupRouteConfig(const Http::RequestHeaderMap& headers, uint64_t random_value) const {
    absl::optional<std::string> corpus_path =
        TestEnvironment::getOptionalEnvVar("GENRULE_OUTPUT_DIR");
    if (corpus_path) {
      static uint32_t n;
      test::common::router::RouteTestCase route_test_case;
      route_test_case.mutable_config()->MergeFrom(config_);
      route_test_case.mutable_headers()->MergeFrom(Fuzz::toHeaders(headers));
      route_test_case.set_random_value(random_value);
      const std::string path = fmt::format("{}/generated_corpus_{}", corpus_path.value(), n++);
      const std::string corpus = route_test_case.DebugString();
      {
        std::ofstream corpus_file(path);
        ENVOY_LOG_MISC(debug, "Writing {} to {}", corpus, path);
        corpus_file << corpus;
      }
    }
  }

  RouteConstSharedPtr route(const Http::RequestHeaderMap& headers,
                            const Envoy::StreamInfo::StreamInfo& stream_info,
                            uint64_t random_value) const override {

    setupRouteConfig(headers, random_value);
    return ConfigImpl::route(headers, stream_info, random_value);
  }

  RouteConstSharedPtr route(const RouteCallback& cb, const Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info,
                            uint64_t random_value) const override {

    setupRouteConfig(headers, random_value);
    return ConfigImpl::route(cb, headers, stream_info, random_value);
  }

  RouteConstSharedPtr route(const RouteCallback& cb, const Http::RequestHeaderMap& headers) const {
    return route(cb, headers, NiceMock<Envoy::StreamInfo::MockStreamInfo>(), 0);
  }

  RouteConstSharedPtr route(const Http::RequestHeaderMap& headers, uint64_t random_value) const {
    return route(headers, NiceMock<Envoy::StreamInfo::MockStreamInfo>(), random_value);
  }

  const envoy::config::route::v3::RouteConfiguration config_;
};

Http::TestRequestHeaderMapImpl genPathlessHeaders(const std::string& host,
                                                  const std::string& method) {
  return Http::TestRequestHeaderMapImpl{{":authority", host},         {":method", method},
                                        {"x-safe", "safe"},           {"x-global-nope", "global"},
                                        {"x-vhost-nope", "vhost"},    {"x-route-nope", "route"},
                                        {"x-forwarded-proto", "http"}};
}

Http::TestRequestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method,
                                          const std::string& forwarded_proto) {
  auto hdrs = Http::TestRequestHeaderMapImpl{
      {":authority", host},        {":path", path},
      {":method", method},         {"x-safe", "safe"},
      {"x-global-nope", "global"}, {"x-vhost-nope", "vhost"},
      {"x-route-nope", "route"},   {"x-forwarded-proto", forwarded_proto}};

  if (forwarded_proto.empty()) {
    hdrs.remove("x-forwarded-proto");
  }

  return hdrs;
}

Http::TestRequestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method) {
  return genHeaders(host, path, method, "http");
}

// Loads a V3 RouteConfiguration yaml
envoy::config::route::v3::RouteConfiguration
parseRouteConfigurationFromYaml(const std::string& yaml) {
  envoy::config::route::v3::RouteConfiguration route_config;
  // Load the file and keep the annotations (in case of an upgrade) to make sure
  // validate() observes the upgrade
  TestUtility::loadFromYaml(yaml, route_config, true);
  TestUtility::validate(route_config);
  return route_config;
}

class ConfigImplTestBase {
protected:
  ConfigImplTestBase() : api_(Api::createApiForTest()) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  std::string virtualHostName(const RouteEntry* route) {
    Stats::StatName name = route->virtualHost().statName();
    return factory_context_.scope().symbolTable().toString(name);
  }

  std::string virtualClusterName(const RouteEntry* route, Http::TestRequestHeaderMapImpl& headers) {
    Stats::StatName name = route->virtualCluster(headers)->statName();
    return factory_context_.scope().symbolTable().toString(name);
  }

  std::string responseHeadersConfig(const bool most_specific_wins, const bool append) const {
    const std::string yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: ["www.lyft.com"]
    response_headers_to_add:
      - header:
          key: x-global-header1
          value: vhost-override
        append: {1}
      - header:
          key: x-vhost-header1
          value: vhost1-www2
        append: {1}
    response_headers_to_remove: ["x-vhost-remove"]
    routes:
      - match:
          prefix: "/new_endpoint"
        route:
          prefix_rewrite: "/api/new_endpoint"
          cluster: www2
        response_headers_to_add:
          - header:
              key: x-route-header
              value: route-override
            append: {1}
          - header:
              key: x-global-header1
              value: route-override
            append: {1}
          - header:
              key: x-vhost-header1
              value: route-override
            append: {1}
      - match:
          path: "/"
        route:
          cluster: root_www2
        response_headers_to_add:
          - header:
              key: x-route-header
              value: route-allpath
            append: {1}
        response_headers_to_remove: ["x-route-remove"]
      - match:
          prefix: "/"
        route:
          cluster: "www2"
  - name: www2_staging
    domains: ["www-staging.lyft.net"]
    response_headers_to_add:
      - header:
          key: x-vhost-header1
          value: vhost1-www2_staging
        append: {1}
    routes:
      - match:
          prefix: "/"
        route:
          cluster: www2_staging
        response_headers_to_add:
          - header:
              key: x-route-header
              value: route-allprefix
            append: {1}
  - name: default
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
        route:
          cluster: "instant-server"
internal_only_headers: ["x-lyft-user-id"]
response_headers_to_add:
  - header:
      key: x-global-header1
      value: global1
    append: {1}
response_headers_to_remove: ["x-global-remove"]
most_specific_header_mutations_wins: {0}
)EOF";

    return fmt::format(yaml, most_specific_wins, append);
  }

  std::string requestHeadersConfig(const bool most_specific_wins) {
    const std::string yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: ["www.lyft.com"]
    request_headers_to_add:
      - header:
          key: x-global-header
          value: vhost-www2
        append: false
      - header:
          key: x-vhost-header
          value: vhost-www2
        append: false
    request_headers_to_remove: ["x-vhost-nope"]
    routes:
      - match:
          prefix: "/endpoint"
        request_headers_to_add:
          - header:
              key: x-global-header
              value: route-endpoint
            append: false
          - header:
              key: x-vhost-header
              value: route-endpoint
            append: false
          - header:
              key: x-route-header
              value: route-endpoint
            append: false
        request_headers_to_remove: ["x-route-nope"]
        route:
          cluster: www2
      - match:
          prefix: "/"
        route:
          cluster: www2
  - name: default
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
        route:
          cluster: default
request_headers_to_add:
  - header:
      key: x-global-header
      value: global
    append: false
request_headers_to_remove: ["x-global-nope"]
most_specific_header_mutations_wins: {0}
)EOF";

    return fmt::format(yaml, most_specific_wins);
  }

  Stats::TestSymbolTable symbol_table_;
  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
};

class RouteMatcherTest : public testing::Test, public ConfigImplTestBase {};

// When removing legacy fields this test can be removed.
TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(TestLegacyRoutes)) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: regex
  domains:
  - bat.com
  routes:
  - match:
      regex: "/t[io]c"
    route:
      cluster: clock
  - match:
      safe_regex:
        google_re2: {}
        regex: "/baa+"
    route:
      cluster: sheep
  - match:
      regex: ".*/\\d{3}$"
    route:
      cluster: three_numbers
      prefix_rewrite: "/rewrote"
  - match:
      regex: ".*"
    route:
      cluster: regex_default
- name: regex2
  domains:
  - bat2.com
  routes:
  - match:
      regex: ''
    route:
      cluster: nothingness
  - match:
      regex: ".*"
    route:
      cluster: regex_default
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: instant-server
      timeout: 30s
  virtual_clusters:
  - pattern: "^/rides$"
    method: POST
    name: ride_request
  - pattern: "^/rides/\\d+$"
    method: PUT
    name: update_ride
  - pattern: "^/users/\\d+/chargeaccounts$"
    method: POST
    name: cc_add
  - pattern: "^/users/\\d+/chargeaccounts/(?!validate)\\w+$"
    method: PUT
    name: cc_add
  - pattern: "^/users$"
    method: POST
    name: create_user_login
  - pattern: "^/users/\\d+$"
    method: PUT
    name: update_user
  )EOF";

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // Regular Expression matching
  EXPECT_EQ("clock",
            config.route(genHeaders("bat.com", "/tic", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("clock",
            config.route(genHeaders("bat.com", "/toc", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tac", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tick", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tic/toc", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("sheep",
            config.route(genHeaders("bat.com", "/baa", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "sheep",
      config.route(genHeaders("bat.com", "/baaaaaaaaaaaa", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/ba", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("nothingness",
            config.route(genHeaders("bat2.com", "", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat2.com", "/foo", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat2.com", " ", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_TRUE(config.route(genPathlessHeaders("bat2.com", "GET"), 0) == nullptr);

  // Regular Expression matching with query string params
  EXPECT_EQ(
      "clock",
      config.route(genHeaders("bat.com", "/tic?tac=true", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "regex_default",
      config.route(genHeaders("bat.com", "/tac?tic=true", "GET"), 0)->routeEntry()->clusterName());

  // Virtual cluster testing.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "GET");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/blah", "POST");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "POST");
    EXPECT_EQ("ride_request", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123", "PUT");
    EXPECT_EQ("update_ride", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123/456", "POST");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts", "POST");
    EXPECT_EQ("cc_add", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/hello123", "PUT");
    EXPECT_EQ("cc_add", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/validate", "PUT");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/foo/bar", "PUT");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users", "POST");
    EXPECT_EQ("create_user_login",
              virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123", "PUT");
    EXPECT_EQ("update_user", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/something/else", "GET");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
}

TEST_F(RouteMatcherTest, TestConnectRoutes) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: connect
  domains:
  - bat3.com
  routes:
  - match:
      safe_regex:
        google_re2: {}
        regex: "foobar"
    route:
      cluster: connect_break
  - match:
        connect_matcher:
          {}
    route:
      cluster: connect_match
      prefix_rewrite: "/rewrote"
  - match:
      safe_regex:
        google_re2: {}
        regex: ".*"
    route:
      cluster: connect_fallthrough
- name: connect2
  domains:
  - bat4.com
  routes:
  - match:
        connect_matcher:
          {}
    redirect: { path_redirect: /new_path }
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: instant-server
      timeout: 30s
  virtual_clusters:
  - headers:
    - name: ":path"
      safe_regex_match:
        google_re2: {}
        regex: "^/users/\\d+/location$"
    - name: ":method"
      exact_match: POST
    name: ulu
  )EOF";
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // Connect matching
  EXPECT_EQ("connect_match",
            config.route(genHeaders("bat3.com", " ", "CONNECT"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "connect_match",
      config.route(genPathlessHeaders("bat3.com", "CONNECT"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("connect_fallthrough",
            config.route(genHeaders("bat3.com", " ", "GET"), 0)->routeEntry()->clusterName());

  // Prefix rewrite for CONNECT with path (for HTTP/2)
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("bat3.com", "/api/locations?works=true", "CONNECT");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/rewrote?works=true", headers.get_(Http::Headers::get().Path));
  }
  // Prefix rewrite for CONNECT without path (for non-crashing)
  {
    Http::TestRequestHeaderMapImpl headers = genPathlessHeaders("bat4.com", "CONNECT");
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    ASSERT(redirect != nullptr);
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://bat4.com/new_path", redirect->newPath(headers));
  }
}

TEST_F(RouteMatcherTest, TestRoutes) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - lyft.com
  - www.lyft.com
  - w.lyft.com
  - ww.lyft.com
  - wwww.lyft.com
  routes:
  - match:
      prefix: "/new_endpoint"
    route:
      prefix_rewrite: "/api/new_endpoint"
      cluster: www2
  - match:
      prefix: "/newforreg1_endpoint"
    route:
      regex_rewrite:
        pattern:
          google_re2: {}
          regex: "^/new(.*?)_endpoint(.*)$"
        substitution: /\1_rewritten_endpoint\2
      cluster: www2
  - match:
      prefix: "/newforreg2_endpoint"
    route:
      regex_rewrite:
        pattern:
          google_re2: {}
          regex: "e"
        substitution: "X"
      cluster: www2
  - match:
      path: "/exact/path/for/regex1"
      case_sensitive: true
    route:
      cluster: www2
      regex_rewrite:
        pattern:
          google_re2: {}
          regex: "[aeioe]"
        substitution: "V"
  - match:
      path: "/"
    route:
      cluster: root_www2
  - match:
      prefix: "/"
    route:
      cluster: www2
- name: www2_staging
  domains:
  - www-staging.lyft.net
  - www-staging-orca.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2_staging
- name: wildcard
  domains:
  - "*.foo.com"
  - "*-bar.baz.com"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: wildcard
- name: wildcard2
  domains:
  - "*.baz.com"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: wildcard2
- name: regex
  domains:
  - bat.com
  routes:
  - match:
      safe_regex:
        google_re2: {}
        regex: "/t[io]c"
    route:
      cluster: clock
  - match:
      safe_regex:
        google_re2: {}
        regex: "/baa+"
    route:
      cluster: sheep
  - match:
      safe_regex:
        google_re2: {}
        regex: ".*/\\d{3}$"
    route:
      cluster: three_numbers
      prefix_rewrite: "/rewrote"
  - match:
      safe_regex:
        google_re2: {}
        regex: ".*/\\d{4}$"
    route:
      cluster: four_numbers
      regex_rewrite:
        pattern:
          google_re2: {}
          regex: "(^.*)/(\\d{4})$"
        substitution: /four/\2/endpoint\1
  - match:
      safe_regex:
        google_re2: {}
        regex: ".*"
    route:
      cluster: regex_default
- name: regex2
  domains:
  - bat2.com
  routes:
  - match:
      safe_regex:
        google_re2: {}
        regex: ".*"
    route:
      cluster: regex_default
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/api/application_data"
    route:
      cluster: ats
  - match:
      path: "/api/locations"
      case_sensitive: false
    route:
      cluster: locations
      prefix_rewrite: "/rewrote"
  - match:
      prefix: "/api/leads/me"
    route:
      cluster: ats
  - match:
      prefix: "/host/rewrite/me"
    route:
      cluster: ats
      host_rewrite: new_host
  - match:
      prefix: "/oldhost/rewrite/me"
    route:
      cluster: ats
      host_rewrite: new_oldhost
  - match:
      path: "/foo"
      case_sensitive: true
    route:
      prefix_rewrite: "/bar"
      cluster: instant-server
  - match:
      path: "/tar"
      case_sensitive: false
    route:
      prefix_rewrite: "/car"
      cluster: instant-server
  - match:
      prefix: "/newhost/rewrite/me"
      case_sensitive: false
    route:
      cluster: ats
      host_rewrite: new_host
  - match:
      path: "/FOOD"
      case_sensitive: false
    route:
      prefix_rewrite: "/cAndy"
      cluster: ats
  - match:
      path: "/ApplEs"
      case_sensitive: true
    route:
      prefix_rewrite: "/oranGES"
      cluster: instant-server
  - match:
      path: "/rewrite-host-with-header-value"
    request_headers_to_add:
    - header:
        key: x-rewrite-host
        value: rewrote
    route:
      cluster: ats
      auto_host_rewrite_header: x-rewrite-host
  - match:
      path: "/do-not-rewrite-host-with-header-value"
    route:
      cluster: ats
      auto_host_rewrite_header: x-rewrite-host
  - match:
      prefix: "/"
    route:
      cluster: instant-server
      timeout: 30s
  virtual_clusters:
  - headers:
    - name: ":path"
      safe_regex_match:
        google_re2: {}
        regex: "^/rides$"
    - name: ":method"
      exact_match: POST
    name: ride_request
  - headers:
    - name: ":path"
      safe_regex_match:
        google_re2: {}
        regex: "^/rides/\\d+$"
    - name: ":method"
      exact_match: PUT
    name: update_ride
  - headers:
    - name: ":path"
      safe_regex_match:
        google_re2: {}
        regex: "^/users/\\d+/chargeaccounts$"
    - name: ":method"
      exact_match: POST
    name: cc_add
  - headers:
    - name: ":path"
      safe_regex_match:
        google_re2: {}
        regex: "^/users$"
    - name: ":method"
      exact_match: POST
    name: create_user_login
  - headers:
    - name: ":path"
      safe_regex_match:
        google_re2: {}
        regex: "^/users/\\d+$"
    - name: ":method"
      exact_match: PUT
    name: update_user
  - headers:
    - name: ":path"
      safe_regex_match:
        google_re2: {}
        regex: "^/users/\\d+/location$"
    - name: ":method"
      exact_match: POST
    name: ulu
  )EOF";
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // No host header, no x-forwarded-proto and no path header testing.
  EXPECT_EQ(nullptr,
            config.route(Http::TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, 0));
  EXPECT_EQ(nullptr, config.route(Http::TestRequestHeaderMapImpl{{":authority", "foo"},
                                                                 {":path", "/"},
                                                                 {":method", "GET"}},
                                  0));
  EXPECT_EQ(nullptr, config.route(Http::TestRequestHeaderMapImpl{{":authority", "foo"},
                                                                 {":method", "CONNECT"},
                                                                 {"x-forwarded-proto", "http"}},
                                  0));

  // Base routing testing.
  EXPECT_EQ("instant-server",
            config.route(genHeaders("api.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("ats", config.route(genHeaders("api.lyft.com", "/api/leads/me", "GET"), 0)
                       ->routeEntry()
                       ->clusterName());
  EXPECT_EQ("ats", config.route(genHeaders("api.lyft.com", "/api/application_data", "GET"), 0)
                       ->routeEntry()
                       ->clusterName());

  EXPECT_EQ("locations",
            config.route(genHeaders("api.lyft.com", "/api/locations?works=true", "GET"), 0)
                ->routeEntry()
                ->clusterName());
  EXPECT_EQ("locations", config.route(genHeaders("api.lyft.com", "/api/locations", "GET"), 0)
                             ->routeEntry()
                             ->clusterName());
  EXPECT_EQ("www2",
            config.route(genHeaders("lyft.com", "/foo", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("root_www2",
            config.route(genHeaders("wwww.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("www2",
            config.route(genHeaders("LYFT.COM", "/foo", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("root_www2",
            config.route(genHeaders("wWww.LyfT.coM", "/", "GET"), 0)->routeEntry()->clusterName());

  // Wildcards
  EXPECT_EQ("wildcard",
            config.route(genHeaders("www.foo.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "wildcard",
      config.route(genHeaders("foo-bar.baz.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("wildcard2",
            config.route(genHeaders("-bar.baz.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("wildcard2",
            config.route(genHeaders("bar.baz.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("instant-server",
            config.route(genHeaders(".foo.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("instant-server",
            config.route(genHeaders("foo.com", "/", "GET"), 0)->routeEntry()->clusterName());

  // Regular Expression matching
  EXPECT_EQ("clock",
            config.route(genHeaders("bat.com", "/tic", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("clock",
            config.route(genHeaders("bat.com", "/toc", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tac", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tick", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tic/toc", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("sheep",
            config.route(genHeaders("bat.com", "/baa", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "sheep",
      config.route(genHeaders("bat.com", "/baaaaaaaaaaaa", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/ba", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat2.com", "/foo", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat2.com", " ", "GET"), 0)->routeEntry()->clusterName());

  // Regular Expression matching with query string params
  EXPECT_EQ(
      "clock",
      config.route(genHeaders("bat.com", "/tic?tac=true", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "regex_default",
      config.route(genHeaders("bat.com", "/tac?tic=true", "GET"), 0)->routeEntry()->clusterName());

  // Timeout testing.
  EXPECT_EQ(std::chrono::milliseconds(30000),
            config.route(genHeaders("api.lyft.com", "/", "GET"), 0)->routeEntry()->timeout());
  EXPECT_EQ(
      std::chrono::milliseconds(15000),
      config.route(genHeaders("api.lyft.com", "/api/leads/me", "GET"), 0)->routeEntry()->timeout());

  // Prefix rewrite testing.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", virtualHostName(route));
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/api/new_endpoint/foo", headers.get_(Http::Headers::get().Path));
    EXPECT_EQ("/new_endpoint/foo", headers.get_(Http::Headers::get().EnvoyOriginalPath));
  }

  // Prefix rewrite testing (x-envoy-* headers suppressed).
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", virtualHostName(route));
    route->finalizeRequestHeaders(headers, stream_info, false);
    EXPECT_EQ("/api/new_endpoint/foo", headers.get_(Http::Headers::get().Path));
    EXPECT_FALSE(headers.has(Http::Headers::get().EnvoyOriginalPath));
  }

  // Prefix rewrite on path match with query string params
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/api/locations?works=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/rewrote?works=true", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/bar", headers.get_(Http::Headers::get().Path));
  }

  // Regular expression path rewrite after prefix match testing.
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/newforreg1_endpoint/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", virtualHostName(route));
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/forreg1_rewritten_endpoint/foo", headers.get_(Http::Headers::get().Path));
    EXPECT_EQ("/newforreg1_endpoint/foo", headers.get_(Http::Headers::get().EnvoyOriginalPath));
  }

  // Regular expression path rewrite after prefix match testing, replace every
  // occurrence, excluding query parameters.
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/newforreg2_endpoint/tee?test=me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", virtualHostName(route));
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/nXwforrXg2_Xndpoint/tXX?test=me", headers.get_(Http::Headers::get().Path));
    EXPECT_EQ("/newforreg2_endpoint/tee?test=me",
              headers.get_(Http::Headers::get().EnvoyOriginalPath));
  }

  // Regular expression path rewrite after exact path match testing.
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/exact/path/for/regex1", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", virtualHostName(route));
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/VxVct/pVth/fVr/rVgVx1", headers.get_(Http::Headers::get().Path));
    EXPECT_EQ("/exact/path/for/regex1", headers.get_(Http::Headers::get().EnvoyOriginalPath));
  }

  // Regular expression path rewrite after exact path match testing,
  // with query parameters.
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/exact/path/for/regex1?test=aeiou", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", virtualHostName(route));
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/VxVct/pVth/fVr/rVgVx1?test=aeiou", headers.get_(Http::Headers::get().Path));
    EXPECT_EQ("/exact/path/for/regex1?test=aeiou",
              headers.get_(Http::Headers::get().EnvoyOriginalPath));
  }

  // Host rewrite testing.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/host/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("new_host", headers.get_(Http::Headers::get().Host));
  }

  // Rewrites host using supplied header.
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/rewrite-host-with-header-value", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("rewrote", headers.get_(Http::Headers::get().Host));
  }

  // Does not rewrite host because of missing header.
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/do-not-rewrite-host-with-header-value", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("api.lyft.com", headers.get_(Http::Headers::get().Host));
  }

  // Case sensitive rewrite matching test.
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/API/locations?works=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/rewrote?works=true", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/fooD", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/cAndy", headers.get_(Http::Headers::get().Path));
  }

  // Case sensitive is set to true and will not rewrite
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/FOO", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/FOO", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/ApPles", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/ApPles", headers.get_(Http::Headers::get().Path));
  }

  // Case insensitive set to false so there is no rewrite
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/oLDhost/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("api.lyft.com", headers.get_(Http::Headers::get().Host));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/Tart", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/Tart", headers.get_(Http::Headers::get().Path));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/newhost/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("new_host", headers.get_(Http::Headers::get().Host));
  }

  // Prefix rewrite for regular expression matching
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("bat.com", "/647", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/rewrote", headers.get_(Http::Headers::get().Path));
  }

  // Prefix rewrite for regular expression matching with query string
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("bat.com", "/970?foo=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/rewrote?foo=true", headers.get_(Http::Headers::get().Path));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("bat.com", "/foo/bar/238?bar=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/rewrote?bar=true", headers.get_(Http::Headers::get().Path));
  }

  // Regular expression rewrite for regular expression matching
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("bat.com", "/xx/yy/6472", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/four/6472/endpoint/xx/yy", headers.get_(Http::Headers::get().Path));
    EXPECT_EQ("/xx/yy/6472", headers.get_(Http::Headers::get().EnvoyOriginalPath));
  }

  // Regular expression rewrite for regular expression matching, with query parameters.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("bat.com", "/xx/yy/6472?test=foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("/four/6472/endpoint/xx/yy?test=foo", headers.get_(Http::Headers::get().Path));
    EXPECT_EQ("/xx/yy/6472?test=foo", headers.get_(Http::Headers::get().EnvoyOriginalPath));
  }

  // Virtual cluster testing.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "GET");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/blah", "POST");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "POST");
    EXPECT_EQ("ride_request", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123", "PUT");
    EXPECT_EQ("update_ride", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123/456", "POST");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/foo/bar", "PUT");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users", "POST");
    EXPECT_EQ("create_user_login",
              virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123", "PUT");
    EXPECT_EQ("update_user", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/location", "POST");
    EXPECT_EQ("ulu", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/something/else", "GET");
    EXPECT_EQ("other", virtualClusterName(config.route(headers, 0)->routeEntry(), headers));
  }
}

TEST_F(RouteMatcherTest, TestRoutesWithWildcardAndDefaultOnly) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: wildcard
    domains: ["*.solo.io"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: "wildcard" }
  - name: default
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: "default" }
  )EOF";

  const auto proto_config = parseRouteConfigurationFromYaml(yaml);
  TestConfigImpl config(proto_config, factory_context_, true);

  EXPECT_EQ("wildcard",
            config.route(genHeaders("gloo.solo.io", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("default",
            config.route(genHeaders("example.com", "/", "GET"), 0)->routeEntry()->clusterName());
}

// When deprecating regex: this test can be removed.
TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(TestRoutesWithInvalidRegexLegacy)) {
  std::string invalid_route = R"EOF(
virtual_hosts:
  - name: regex
    domains: ["*"]
    routes:
      - match: { regex: "/(+invalid)" }
        route: { cluster: "regex" }
  )EOF";

  std::string invalid_virtual_cluster = R"EOF(
virtual_hosts:
  - name: regex
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: "regex" }
    virtual_clusters:
      - pattern: "^/(+invalid)"
        name: "invalid"
  )EOF";

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(invalid_route), factory_context_, true),
      EnvoyException, "Invalid regex '/\\(\\+invalid\\)':");

  EXPECT_THROW_WITH_REGEX(TestConfigImpl(parseRouteConfigurationFromYaml(invalid_virtual_cluster),
                                         factory_context_, true),
                          EnvoyException, "Invalid regex '\\^/\\(\\+invalid\\)':");
}

TEST_F(RouteMatcherTest, TestRoutesWithInvalidRegex) {
  std::string invalid_route = R"EOF(
virtual_hosts:
  - name: regex
    domains: ["*"]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/(+invalid)"
        route: { cluster: "regex" }
  )EOF";

  std::string invalid_virtual_cluster = R"EOF(
virtual_hosts:
  - name: regex
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: "regex" }
    virtual_clusters:
      name: "invalid"
      headers:
        name: "invalid"
        safe_regex_match:
          google_re2: {}
          regex: "^/(+invalid)"
  )EOF";

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(invalid_route), factory_context_, true),
      EnvoyException, "no argument for repetition operator:");

  EXPECT_THROW_WITH_REGEX(TestConfigImpl(parseRouteConfigurationFromYaml(invalid_virtual_cluster),
                                         factory_context_, true),
                          EnvoyException, "no argument for repetition operator");
}

// Virtual cluster that contains neither pattern nor regex. This must be checked while pattern is
// deprecated.
TEST_F(RouteMatcherTest, TestRoutesWithInvalidVirtualCluster) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: "regex" }
    virtual_clusters:
      - name: "invalid"
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "virtual clusters must define either 'pattern' or 'headers'");
}

// Validates behavior of request_headers_to_add at router, vhost, and route levels.
TEST_F(RouteMatcherTest, TestAddRemoveRequestHeaders) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - lyft.com
  - www.lyft.com
  - w.lyft.com
  - ww.lyft.com
  - wwww.lyft.com
  request_headers_to_add:
  - header:
      key: x-global-header1
      value: vhost-override
  - header:
      key: x-vhost-header1
      value: vhost1-www2
  routes:
  - match:
      prefix: "/new_endpoint"
    route:
      prefix_rewrite: "/api/new_endpoint"
      cluster: www2
    request_headers_to_add:
    - header:
        key: x-global-header1
        value: route-override
    - header:
        key: x-vhost-header1
        value: route-override
    - header:
        key: x-route-header
        value: route-new_endpoint
  - match:
      path: "/"
    route:
      cluster: root_www2
    request_headers_to_add:
    - header:
        key: x-route-header
        value: route-allpath
  - match:
      prefix: "/"
    route:
      cluster: www2
- name: www2_staging
  domains:
  - www-staging.lyft.net
  - www-staging-orca.lyft.com
  request_headers_to_add:
  - header:
      key: x-vhost-header1
      value: vhost1-www2_staging
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2_staging
    request_headers_to_add:
    - header:
        key: x-route-header
        value: route-allprefix
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: instant-server
      timeout: 3s
internal_only_headers:
- x-lyft-user-id
response_headers_to_add:
- header:
    key: x-envoy-upstream-canary
    value: 'true'
response_headers_to_remove:
- x-envoy-upstream-canary
- x-envoy-virtual-cluster
request_headers_to_add:
- header:
    key: x-global-header1
    value: global1
  )EOF";

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // Request header manipulation testing.
  {
    {
      Http::TestRequestHeaderMapImpl headers =
          genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, stream_info, true);
      EXPECT_EQ("route-override", headers.get_("x-global-header1"));
      EXPECT_EQ("route-override", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-new_endpoint", headers.get_("x-route-header"));
    }

    // Multiple routes can have same route-level headers with different values.
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, stream_info, true);
      EXPECT_EQ("vhost-override", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allpath", headers.get_("x-route-header"));
    }

    // Multiple virtual hosts can have same virtual host level headers with different values.
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("www-staging.lyft.net", "/foo", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, stream_info, true);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2_staging", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allprefix", headers.get_("x-route-header"));
    }

    // Global headers.
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("api.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, stream_info, true);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
    }
  }
}

// Validates behavior of request_headers_to_add at router, vhost, and route levels when append is
// disabled.
TEST_F(RouteMatcherTest, TestRequestHeadersToAddWithAppendFalse) {
  const std::string yaml = requestHeadersConfig(false);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  envoy::config::route::v3::RouteConfiguration route_config = parseRouteConfigurationFromYaml(yaml);

  TestConfigImpl config(route_config, factory_context_, true);

  // Request header manipulation testing.
  {
    // Global and virtual host override route, route overrides route action.
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/endpoint", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, stream_info, true);
      // Added headers.
      EXPECT_EQ("global", headers.get_("x-global-header"));
      EXPECT_EQ("vhost-www2", headers.get_("x-vhost-header"));
      EXPECT_EQ("route-endpoint", headers.get_("x-route-header"));
      // Removed headers.
      EXPECT_FALSE(headers.has("x-global-nope"));
      EXPECT_FALSE(headers.has("x-vhost-nope"));
      EXPECT_FALSE(headers.has("x-route-nope"));
    }

    // Global overrides virtual host.
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, stream_info, true);
      // Added headers.
      EXPECT_EQ("global", headers.get_("x-global-header"));
      EXPECT_EQ("vhost-www2", headers.get_("x-vhost-header"));
      EXPECT_FALSE(headers.has("x-route-header"));
      // Removed headers.
      EXPECT_FALSE(headers.has("x-global-nope"));
      EXPECT_FALSE(headers.has("x-vhost-nope"));
      EXPECT_TRUE(headers.has("x-route-nope"));
    }

    // Global only.
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("www.example.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, stream_info, true);
      // Added headers.
      EXPECT_EQ("global", headers.get_("x-global-header"));
      EXPECT_FALSE(headers.has("x-vhost-header"));
      EXPECT_FALSE(headers.has("x-route-header"));
      // Removed headers.
      EXPECT_FALSE(headers.has("x-global-nope"));
      EXPECT_TRUE(headers.has("x-vhost-nope"));
      EXPECT_TRUE(headers.has("x-route-nope"));
    }
  }
}

TEST_F(RouteMatcherTest, TestRequestHeadersToAddWithAppendFalseMostSpecificWins) {
  const std::string yaml = requestHeadersConfig(true);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // Route overrides vhost and global.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/endpoint", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    // Added headers.
    EXPECT_EQ("route-endpoint", headers.get_("x-global-header"));
    EXPECT_EQ("route-endpoint", headers.get_("x-vhost-header"));
    EXPECT_EQ("route-endpoint", headers.get_("x-route-header"));
    // Removed headers.
    EXPECT_FALSE(headers.has("x-global-nope"));
    EXPECT_FALSE(headers.has("x-vhost-nope"));
    EXPECT_FALSE(headers.has("x-route-nope"));
  }

  // Virtual overrides global.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, stream_info, true);
    // Added headers.
    EXPECT_EQ("vhost-www2", headers.get_("x-global-header"));
    EXPECT_EQ("vhost-www2", headers.get_("x-vhost-header"));
    EXPECT_FALSE(headers.has("x-route-header"));
    // Removed headers.
    EXPECT_FALSE(headers.has("x-global-nope"));
    EXPECT_FALSE(headers.has("x-vhost-nope"));
    EXPECT_TRUE(headers.has("x-route-nope"));
  }
}

// Validates behavior of response_headers_to_add and response_headers_to_remove at router, vhost,
// and route levels.
TEST_F(RouteMatcherTest, TestAddRemoveResponseHeaders) {
  const std::string yaml = responseHeadersConfig(false, true);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // Response header manipulation testing.
  {
    {
      Http::TestRequestHeaderMapImpl req_headers =
          genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestResponseHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, stream_info);
      EXPECT_EQ("route-override", headers.get_("x-global-header1"));
      EXPECT_EQ("route-override", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-override", headers.get_("x-route-header"));
    }

    // Multiple routes can have same route-level headers with different values.
    {
      Http::TestRequestHeaderMapImpl req_headers = genHeaders("www.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestResponseHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, stream_info);
      EXPECT_EQ("vhost-override", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allpath", headers.get_("x-route-header"));
    }

    // Multiple virtual hosts can have same virtual host level headers with different values.
    {
      Http::TestRequestHeaderMapImpl req_headers =
          genHeaders("www-staging.lyft.net", "/foo", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestResponseHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, stream_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2_staging", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allprefix", headers.get_("x-route-header"));
    }

    // Global headers.
    {
      Http::TestRequestHeaderMapImpl req_headers = genHeaders("api.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestResponseHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, stream_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
    }
  }

  EXPECT_THAT(std::list<Http::LowerCaseString>{Http::LowerCaseString("x-lyft-user-id")},
              ContainerEq(config.internalOnlyHeaders()));
}

TEST_F(RouteMatcherTest, TestAddRemoveResponseHeadersAppendFalse) {
  const std::string yaml = responseHeadersConfig(false, false);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  Http::TestRequestHeaderMapImpl req_headers =
      genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
  const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
  Http::TestResponseHeaderMapImpl headers;
  route->finalizeResponseHeaders(headers, stream_info);
  EXPECT_EQ("global1", headers.get_("x-global-header1"));
  EXPECT_EQ("vhost1-www2", headers.get_("x-vhost-header1"));
  EXPECT_EQ("route-override", headers.get_("x-route-header"));
}

TEST_F(RouteMatcherTest, TestAddRemoveResponseHeadersAppendMostSpecificWins) {
  const std::string yaml = responseHeadersConfig(true, false);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  Http::TestRequestHeaderMapImpl req_headers =
      genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
  const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
  Http::TestResponseHeaderMapImpl headers;
  route->finalizeResponseHeaders(headers, stream_info);
  EXPECT_EQ("route-override", headers.get_("x-global-header1"));
  EXPECT_EQ("route-override", headers.get_("x-vhost-header1"));
  EXPECT_EQ("route-override", headers.get_("x-route-header"));
}

TEST_F(RouteMatcherTest, TestAddGlobalResponseHeaderRemoveFromRoute) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: ["www.lyft.com"]
    routes:
      - match:
          prefix: "/cacheable"
        route:
          cluster: www2
        response_headers_to_remove: ["cache-control"]
      - match:
          prefix: "/"
        route:
          cluster: "www2"
response_headers_to_add:
  - header:
      key: cache-control
      value: private
most_specific_header_mutations_wins: true
)EOF";
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl req_headers = genHeaders("www.lyft.com", "/cacheable", "GET");
    const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
    Http::TestResponseHeaderMapImpl headers;
    route->finalizeResponseHeaders(headers, stream_info);
    EXPECT_FALSE(headers.has("cache-control"));
  }

  {
    Http::TestRequestHeaderMapImpl req_headers = genHeaders("www.lyft.com", "/foo", "GET");
    const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
    Http::TestResponseHeaderMapImpl headers;
    route->finalizeResponseHeaders(headers, stream_info);
    EXPECT_EQ("private", headers.get_("cache-control"));
  }
}

// Validate that we can't add :-prefixed request headers.
TEST_F(RouteMatcherTest, TestRequestHeadersToAddNoPseudoHeader) {
  for (const std::string& header :
       {":path", ":authority", ":method", ":scheme", ":status", ":protocol"}) {
    const std::string yaml = fmt::format(R"EOF(
virtual_hosts:
  - name: www2
    domains: ["*"]
    request_headers_to_add:
      - header:
          key: {}
          value: vhost-www2
        append: false
)EOF",
                                         header);

    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

    envoy::config::route::v3::RouteConfiguration route_config =
        parseRouteConfigurationFromYaml(yaml);

    EXPECT_THROW_WITH_MESSAGE(TestConfigImpl config(route_config, factory_context_, true),
                              EnvoyException, ":-prefixed headers may not be modified");
  }
}

// Validate that we can't remove :-prefixed request headers.
TEST_F(RouteMatcherTest, TestRequestHeadersToRemoveNoPseudoHeader) {
  for (const std::string& header :
       {":path", ":authority", ":method", ":scheme", ":status", ":protocol", "host"}) {
    const std::string yaml = fmt::format(R"EOF(
virtual_hosts:
  - name: www2
    domains: ["*"]
    request_headers_to_remove:
      - {}
)EOF",
                                         header);

    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

    envoy::config::route::v3::RouteConfiguration route_config =
        parseRouteConfigurationFromYaml(yaml);

    EXPECT_THROW_WITH_MESSAGE(TestConfigImpl config(route_config, factory_context_, true),
                              EnvoyException, ":-prefixed or host headers may not be removed");
  }
}

TEST_F(RouteMatcherTest, Priority) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: local_service_grpc
      priority: high
  - match:
      prefix: "/bar"
    route:
      cluster: local_service_grpc
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(Upstream::ResourcePriority::High,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)->routeEntry()->priority());
  EXPECT_EQ(Upstream::ResourcePriority::Default,
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry()->priority());
}

TEST_F(RouteMatcherTest, NoHostRewriteAndAutoRewrite) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: local_service
      host_rewrite: foo
      auto_host_rewrite: true
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, NoHostRewriteAndAutoRewriteHeader) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: local_service
      host_rewrite: foo
      auto_host_rewrite_header: "dummy-header"
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, NoAutoRewriteAndAutoRewriteHeader) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: local_service
      auto_host_rewrite: true
      auto_host_rewrite_header: "dummy-header"
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, HeaderMatchedRouting) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
      headers:
      - name: test_header
        exact_match: test
    route:
      cluster: local_service_with_headers
  - match:
      prefix: "/"
      headers:
      - name: test_header_multiple1
        exact_match: test1
      - name: test_header_multiple2
        exact_match: test2
    route:
      cluster: local_service_with_multiple_headers
  - match:
      prefix: "/"
      headers:
      - name: test_header_presence
        present_match: true
    route:
      cluster: local_service_with_empty_headers
  - match:
      prefix: "/"
      headers:
      - name: test_header_pattern
        safe_regex_match:
          google_re2: {}
          regex: "^user=test-\\d+$"
    route:
      cluster: local_service_with_header_pattern_set_regex
  - match:
      prefix: "/"
      headers:
      - name: test_header_pattern
        exact_match: "^customer=test-\\d+$"
    route:
      cluster: local_service_with_header_pattern_unset_regex
  - match:
      prefix: "/"
      headers:
      - name: test_header_range
        range_match:
          start: 1
          end: 10
    route:
      cluster: local_service_with_header_range
  - match:
      prefix: "/"
    route:
      cluster: local_service_without_headers
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    EXPECT_EQ("local_service_without_headers",
              config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header", "test");
    EXPECT_EQ("local_service_with_headers", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_multiple1", "test1");
    headers.addCopy("test_header_multiple2", "test2");
    EXPECT_EQ("local_service_with_multiple_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("non_existent_header", "foo");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_presence", "test");
    EXPECT_EQ("local_service_with_empty_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "user=test-1223");
    EXPECT_EQ("local_service_with_header_pattern_set_regex",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "customer=test-1223");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_range", "9");
    EXPECT_EQ("local_service_with_header_range",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_range", "19");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
}

// Verify the fixes for https://github.com/envoyproxy/envoy/issues/2406
// When removing regex_match this test can be removed entirely.
TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(InvalidHeaderMatchedRoutingConfigLegacy)) {
  std::string value_with_regex_chars = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          headers:
            - name: test_header
              exact_match: "(+not a regex)"
        route: { cluster: "local_service" }
  )EOF";

  std::string invalid_regex = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          headers:
            - name: test_header
              regex_match: "(+invalid regex)"
        route: { cluster: "local_service" }
  )EOF";

  EXPECT_NO_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(value_with_regex_chars),
                                 factory_context_, true));

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(invalid_regex), factory_context_, true),
      EnvoyException, "Invalid regex");
}

// Verify the fixes for https://github.com/envoyproxy/envoy/issues/2406
TEST_F(RouteMatcherTest, InvalidHeaderMatchedRoutingConfig) {
  std::string value_with_regex_chars = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          headers:
            - name: test_header
              exact_match: "(+not a regex)"
        route: { cluster: "local_service" }
  )EOF";

  std::string invalid_regex = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          headers:
            - name: test_header
              safe_regex_match:
                google_re2: {}
                regex: "(+invalid regex)"
        route: { cluster: "local_service" }
  )EOF";

  EXPECT_NO_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(value_with_regex_chars),
                                 factory_context_, true));

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(invalid_regex), factory_context_, true),
      EnvoyException, "no argument for repetition operator");
}

// When removing value: simply remove that section of the config and the relevant test.
TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(QueryParamMatchedRouting)) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
      query_parameters:
      - name: id
        value: "\\d+[02468]"
        regex: true
      - name: debug
    route:
      cluster: local_service_with_multiple_query_parameters
  - match:
      prefix: "/"
      query_parameters:
      - name: param
        value: test
    route:
      cluster: local_service_with_query_parameter
  - match:
      prefix: "/"
      query_parameters:
      - name: debug
    route:
      cluster: local_service_with_valueless_query_parameter
  - match:
      prefix: "/"
      query_parameters:
      - name: debug2
        present_match: true
    route:
      cluster: local_service_with_present_match_query_parameter
  - match:
      prefix: "/"
      query_parameters:
      - name: debug3
        string_match:
          exact: foo
    route:
      cluster: local_service_with_string_match_query_parameter
  - match:
      prefix: "/"
    route:
      cluster: local_service_without_query_parameters

  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/", "GET");
    EXPECT_EQ("local_service_without_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/?", "GET");
    EXPECT_EQ("local_service_without_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/?param=testing", "GET");
    EXPECT_EQ("local_service_without_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/?param=test", "GET");
    EXPECT_EQ("local_service_with_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/?debug", "GET");
    EXPECT_EQ("local_service_with_valueless_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/?debug2", "GET");
    EXPECT_EQ("local_service_with_present_match_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/?debug3=foo", "GET");
    EXPECT_EQ("local_service_with_string_match_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("example.com", "/?debug=2", "GET");
    EXPECT_EQ("local_service_with_valueless_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("example.com", "/?param=test&debug&id=01", "GET");
    EXPECT_EQ("local_service_with_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers =
        genHeaders("example.com", "/?param=test&debug&id=02", "GET");
    EXPECT_EQ("local_service_with_multiple_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
}

// When removing value: this test can be removed.
TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(InvalidQueryParamMatchedRoutingConfig)) {
  std::string value_with_regex_chars = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          query_parameters:
            - name: test_param
              value: "(+not a regex)"
        route: { cluster: "local_service" }
  )EOF";

  std::string invalid_regex = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          query_parameters:
            - name: test_param
              value: "(+invalid regex)"
              regex: true
        route: { cluster: "local_service" }
  )EOF";

  EXPECT_NO_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(value_with_regex_chars),
                                 factory_context_, true));

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(invalid_regex), factory_context_, true),
      EnvoyException, "Invalid regex");
}

class RouterMatcherHashPolicyTest : public testing::Test, public ConfigImplTestBase {
protected:
  RouterMatcherHashPolicyTest()
      : add_cookie_nop_(
            [](const std::string&, const std::string&, std::chrono::seconds) { return ""; }) {
    const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: foo
  - match:
      prefix: "/bar"
    route:
      cluster: bar
  )EOF";
    route_config_ = parseRouteConfigurationFromYaml(yaml);
  }

  envoy::config::route::v3::RouteAction::HashPolicy* firstRouteHashPolicy() {
    auto hash_policies = route_config_.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_hash_policy();
    if (!hash_policies->empty()) {
      return hash_policies->Mutable(0);
    } else {
      return hash_policies->Add();
    }
  }

  TestConfigImpl& config() {
    if (config_ == nullptr) {
      config_ = std::make_unique<TestConfigImpl>(route_config_, factory_context_, true);
    }
    return *config_;
  }

  envoy::config::route::v3::RouteConfiguration route_config_;
  Http::HashPolicy::AddCookieCallback add_cookie_nop_;

private:
  std::unique_ptr<TestConfigImpl> config_;
};

TEST_F(RouterMatcherHashPolicyTest, HashHeaders) {
  firstRouteHashPolicy()->mutable_header()->set_header_name("foo_header");
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                 nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashHeadersRegexSubstitution) {
  // Apply a regex substitution before hashing.
  auto* header = firstRouteHashPolicy()->mutable_header();
  header->set_header_name(":path");
  auto* regex_spec = header->mutable_regex_rewrite();
  regex_spec->set_substitution("\\1");
  auto* pattern = regex_spec->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex("^/(\\w+)$");
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    const auto foo_hash_value = 3728699739546630719;
    EXPECT_EQ(route->routeEntry()
                  ->hashPolicy()
                  ->generateHash(nullptr, headers, add_cookie_nop_, nullptr)
                  .value(),
              foo_hash_value);
  }
}

class RouterMatcherCookieHashPolicyTest : public RouterMatcherHashPolicyTest {
public:
  RouterMatcherCookieHashPolicyTest() {
    firstRouteHashPolicy()->mutable_cookie()->set_name("hash");
  }
};

TEST_F(RouterMatcherCookieHashPolicyTest, NoTtl) {
  {
    // With no cookie, no hash is generated.
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                 nullptr));
  }
  {
    // With no matching cookie, no hash is generated.
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; su=gar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                 nullptr));
  }
  {
    // Matching cookie produces a valid hash.
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; hash=brown");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                nullptr));
  }
  {
    // The hash policy is per-route.
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherCookieHashPolicyTest, DifferentCookies) {
  // Different cookies produce different hashes.
  uint64_t hash_1, hash_2;
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "hash=brown");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_1 = route->routeEntry()
                 ->hashPolicy()
                 ->generateHash(nullptr, headers, add_cookie_nop_, nullptr)
                 .value();
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "hash=green");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_2 = route->routeEntry()
                 ->hashPolicy()
                 ->generateHash(nullptr, headers, add_cookie_nop_, nullptr)
                 .value();
  }
  EXPECT_NE(hash_1, hash_2);
}

TEST_F(RouterMatcherCookieHashPolicyTest, TtlSet) {
  firstRouteHashPolicy()->mutable_cookie()->mutable_ttl()->set_seconds(42);

  MockFunction<std::string(const std::string&, const std::string&, long)> mock_cookie_cb;
  auto add_cookie = [&mock_cookie_cb](const std::string& name, const std::string& path,
                                      std::chrono::seconds ttl) -> std::string {
    return mock_cookie_cb.Call(name, path, ttl.count());
  };

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_CALL(mock_cookie_cb, Call("hash", "", 42));
    EXPECT_TRUE(
        route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie, nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; su=gar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_CALL(mock_cookie_cb, Call("hash", "", 42));
    EXPECT_TRUE(
        route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie, nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; hash=brown");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(
        route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie, nullptr));
  }
  {
    uint64_t hash_1, hash_2;
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
      Router::RouteConstSharedPtr route = config().route(headers, 0);
      EXPECT_CALL(mock_cookie_cb, Call("hash", "", 42)).WillOnce(Return("AAAAAAA"));
      hash_1 = route->routeEntry()
                   ->hashPolicy()
                   ->generateHash(nullptr, headers, add_cookie, nullptr)
                   .value();
    }
    {
      Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
      Router::RouteConstSharedPtr route = config().route(headers, 0);
      EXPECT_CALL(mock_cookie_cb, Call("hash", "", 42)).WillOnce(Return("BBBBBBB"));
      hash_2 = route->routeEntry()
                   ->hashPolicy()
                   ->generateHash(nullptr, headers, add_cookie, nullptr)
                   .value();
    }
    EXPECT_NE(hash_1, hash_2);
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherCookieHashPolicyTest, SetSessionCookie) {
  firstRouteHashPolicy()->mutable_cookie()->mutable_ttl()->set_seconds(0);

  MockFunction<std::string(const std::string&, const std::string&, long)> mock_cookie_cb;
  auto add_cookie = [&mock_cookie_cb](const std::string& name, const std::string& path,
                                      std::chrono::seconds ttl) -> std::string {
    return mock_cookie_cb.Call(name, path, ttl.count());
  };

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_CALL(mock_cookie_cb, Call("hash", "", 0));
    EXPECT_TRUE(
        route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie, nullptr));
  }
}

TEST_F(RouterMatcherCookieHashPolicyTest, SetCookiePath) {
  firstRouteHashPolicy()->mutable_cookie()->mutable_ttl()->set_seconds(0);
  firstRouteHashPolicy()->mutable_cookie()->set_path("/");

  MockFunction<std::string(const std::string&, const std::string&, long)> mock_cookie_cb;
  auto add_cookie = [&mock_cookie_cb](const std::string& name, const std::string& path,
                                      std::chrono::seconds ttl) -> std::string {
    return mock_cookie_cb.Call(name, path, ttl.count());
  };

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_CALL(mock_cookie_cb, Call("hash", "/", 0));
    EXPECT_TRUE(
        route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie, nullptr));
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashIp) {
  Network::Address::Ipv4Instance valid_address("1.2.3.4");
  firstRouteHashPolicy()->mutable_connection_properties()->set_source_ip(true);
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                 nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash(&valid_address, headers,
                                                                add_cookie_nop_, nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    uint64_t old_hash = config()
                            .route(headers, 0)
                            ->routeEntry()
                            ->hashPolicy()
                            ->generateHash(&valid_address, headers, add_cookie_nop_, nullptr)
                            .value();
    headers.addCopy("foo_header", "bar");
    EXPECT_EQ(old_hash, config()
                            .route(headers, 0)
                            ->routeEntry()
                            ->hashPolicy()
                            ->generateHash(&valid_address, headers, add_cookie_nop_, nullptr)
                            .value());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashIpNonIpAddress) {
  NiceMock<Network::MockIp> bad_ip;
  NiceMock<Network::MockResolvedAddress> bad_ip_address("", "");
  firstRouteHashPolicy()->mutable_connection_properties()->set_source_ip(true);
  {
    ON_CALL(bad_ip_address, ip()).WillByDefault(Return(nullptr));
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(&bad_ip_address, headers,
                                                                 add_cookie_nop_, nullptr));
  }
  {
    const std::string empty;
    ON_CALL(bad_ip_address, ip()).WillByDefault(Return(&bad_ip));
    ON_CALL(bad_ip, addressAsString()).WillByDefault(ReturnRef(empty));
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(&bad_ip_address, headers,
                                                                 add_cookie_nop_, nullptr));
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashIpv4DifferentAddresses) {
  firstRouteHashPolicy()->mutable_connection_properties()->set_source_ip(true);
  {
    // Different addresses should produce different hashes.
    Network::Address::Ipv4Instance first_ip("1.2.3.4");
    Network::Address::Ipv4Instance second_ip("4.3.2.1");
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    const auto hash_policy = config().route(headers, 0)->routeEntry()->hashPolicy();
    const uint64_t hash_1 =
        hash_policy->generateHash(&first_ip, headers, add_cookie_nop_, nullptr).value();
    const uint64_t hash_2 =
        hash_policy->generateHash(&second_ip, headers, add_cookie_nop_, nullptr).value();
    EXPECT_NE(hash_1, hash_2);
  }
  {
    // Same IP addresses but different ports should produce the same hash.
    Network::Address::Ipv4Instance first_ip("1.2.3.4", 8081);
    Network::Address::Ipv4Instance second_ip("1.2.3.4", 1331);
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    const auto hash_policy = config().route(headers, 0)->routeEntry()->hashPolicy();
    const uint64_t hash_1 =
        hash_policy->generateHash(&first_ip, headers, add_cookie_nop_, nullptr).value();
    const uint64_t hash_2 =
        hash_policy->generateHash(&second_ip, headers, add_cookie_nop_, nullptr).value();
    EXPECT_EQ(hash_1, hash_2);
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashIpv6DifferentAddresses) {
  firstRouteHashPolicy()->mutable_connection_properties()->set_source_ip(true);
  {
    // Different addresses should produce different hashes.
    Network::Address::Ipv6Instance first_ip("2001:0db8:85a3:0000:0000::");
    Network::Address::Ipv6Instance second_ip("::1");
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    const auto hash_policy = config().route(headers, 0)->routeEntry()->hashPolicy();
    const uint64_t hash_1 =
        hash_policy->generateHash(&first_ip, headers, add_cookie_nop_, nullptr).value();
    const uint64_t hash_2 =
        hash_policy->generateHash(&second_ip, headers, add_cookie_nop_, nullptr).value();
    EXPECT_NE(hash_1, hash_2);
  }
  {
    // Same IP addresses but different ports should produce the same hash.
    Network::Address::Ipv6Instance first_ip("1:2:3:4:5::", 8081);
    Network::Address::Ipv6Instance second_ip("1:2:3:4:5::", 1331);
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    const auto hash_policy = config().route(headers, 0)->routeEntry()->hashPolicy();
    const uint64_t hash_1 =
        hash_policy->generateHash(&first_ip, headers, add_cookie_nop_, nullptr).value();
    const uint64_t hash_2 =
        hash_policy->generateHash(&second_ip, headers, add_cookie_nop_, nullptr).value();
    EXPECT_EQ(hash_1, hash_2);
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashQueryParameters) {
  firstRouteHashPolicy()->mutable_query_parameter()->set_name("param");
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                 nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo?param=xyz", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar?param=xyz", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy());
  }
}

class RouterMatcherFilterStateHashPolicyTest : public RouterMatcherHashPolicyTest {
public:
  RouterMatcherFilterStateHashPolicyTest()
      : filter_state_(std::make_shared<StreamInfo::FilterStateImpl>(
            StreamInfo::FilterState::LifeSpan::FilterChain)) {

    filter_state_->setData("null-value", nullptr, StreamInfo::FilterState::StateType::ReadOnly,
                           StreamInfo::FilterState::LifeSpan::FilterChain);
    filter_state_->setData("nonhashable", std::make_unique<NonHashable>(),
                           StreamInfo::FilterState::StateType::ReadOnly,
                           StreamInfo::FilterState::LifeSpan::FilterChain);
    filter_state_->setData("hashable", std::make_unique<HashableObj>(),
                           StreamInfo::FilterState::StateType::ReadOnly,
                           StreamInfo::FilterState::LifeSpan::FilterChain);
  }
  class NonHashable : public StreamInfo::FilterState::Object {};
  class HashableObj : public StreamInfo::FilterState::Object, public Http::Hashable {
    absl::optional<uint64_t> hash() const override { return 12345; };
  };

protected:
  StreamInfo::FilterStateSharedPtr filter_state_;
  Http::TestRequestHeaderMapImpl headers_{genHeaders("www.lyft.com", "/foo", "GET")};
};

// No such key.
TEST_F(RouterMatcherFilterStateHashPolicyTest, KeyNotFound) {
  firstRouteHashPolicy()->mutable_filter_state()->set_key("not-in-filterstate");
  Router::RouteConstSharedPtr route = config().route(headers_, 0);
  EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers_, add_cookie_nop_,
                                                               filter_state_));
}
// Key has no value.
TEST_F(RouterMatcherFilterStateHashPolicyTest, NullValue) {
  firstRouteHashPolicy()->mutable_filter_state()->set_key("null-value");
  Router::RouteConstSharedPtr route = config().route(headers_, 0);
  EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers_, add_cookie_nop_,
                                                               filter_state_));
}
// Nonhashable.
TEST_F(RouterMatcherFilterStateHashPolicyTest, ValueNonHashable) {
  firstRouteHashPolicy()->mutable_filter_state()->set_key("nonhashable");
  Router::RouteConstSharedPtr route = config().route(headers_, 0);
  EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers_, add_cookie_nop_,
                                                               filter_state_));
}
// Hashable Key.
TEST_F(RouterMatcherFilterStateHashPolicyTest, Hashable) {
  firstRouteHashPolicy()->mutable_filter_state()->set_key("hashable");
  Router::RouteConstSharedPtr route = config().route(headers_, 0);
  const auto h = route->routeEntry()->hashPolicy()->generateHash(nullptr, headers_, add_cookie_nop_,
                                                                 filter_state_);
  EXPECT_TRUE(h);
  EXPECT_EQ(h, 12345UL);
}

TEST_F(RouterMatcherHashPolicyTest, HashMultiple) {
  auto route = route_config_.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
  route->add_hash_policy()->mutable_header()->set_header_name("foo_header");
  route->add_hash_policy()->mutable_connection_properties()->set_source_ip(true);
  Network::Address::Ipv4Instance address("4.3.2.1");

  uint64_t hash_h, hash_ip, hash_both;
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash(nullptr, headers, add_cookie_nop_,
                                                                 nullptr));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_h = route->routeEntry()
                 ->hashPolicy()
                 ->generateHash(nullptr, headers, add_cookie_nop_, nullptr)
                 .value();
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_ip = route->routeEntry()
                  ->hashPolicy()
                  ->generateHash(&address, headers, add_cookie_nop_, nullptr)
                  .value();
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    headers.addCopy("foo_header", "bar");
    hash_both = route->routeEntry()
                    ->hashPolicy()
                    ->generateHash(&address, headers, add_cookie_nop_, nullptr)
                    .value();
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    headers.addCopy("foo_header", "bar");
    // stability
    EXPECT_EQ(hash_both, route->routeEntry()
                             ->hashPolicy()
                             ->generateHash(&address, headers, add_cookie_nop_, nullptr)
                             .value());
  }
  EXPECT_NE(hash_ip, hash_h);
  EXPECT_NE(hash_ip, hash_both);
  EXPECT_NE(hash_h, hash_both);
}

TEST_F(RouterMatcherHashPolicyTest, HashTerminal) {
  // Hash policy list: cookie, header [terminal=true], user_ip.
  auto route = route_config_.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
  route->add_hash_policy()->mutable_cookie()->set_name("cookie_hash");
  auto* header_hash = route->add_hash_policy();
  header_hash->mutable_header()->set_header_name("foo_header");
  header_hash->set_terminal(true);
  route->add_hash_policy()->mutable_connection_properties()->set_source_ip(true);
  Network::Address::Ipv4Instance address1("4.3.2.1");
  Network::Address::Ipv4Instance address2("1.2.3.4");

  uint64_t hash_1, hash_2;
  // Test terminal works when there is hash computed, the rest of the policy
  // list is ignored.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "cookie_hash=foo;");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_1 = route->routeEntry()
                 ->hashPolicy()
                 ->generateHash(&address1, headers, add_cookie_nop_, nullptr)
                 .value();
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "cookie_hash=foo;");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_2 = route->routeEntry()
                 ->hashPolicy()
                 ->generateHash(&address2, headers, add_cookie_nop_, nullptr)
                 .value();
  }
  EXPECT_EQ(hash_1, hash_2);

  // If no hash computed after evaluating a hash policy, the rest of the policy
  // list is evaluated.
  {
    // Input: {}, {}, address1. Hash on address1.
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_1 = route->routeEntry()
                 ->hashPolicy()
                 ->generateHash(&address1, headers, add_cookie_nop_, nullptr)
                 .value();
  }
  {
    // Input: {}, {}, address2. Hash on address2.
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_2 = route->routeEntry()
                 ->hashPolicy()
                 ->generateHash(&address2, headers, add_cookie_nop_, nullptr)
                 .value();
  }
  EXPECT_NE(hash_1, hash_2);
}

TEST_F(RouterMatcherHashPolicyTest, InvalidHashPolicies) {
  {
    auto hash_policy = firstRouteHashPolicy();
    EXPECT_EQ(envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::
                  POLICY_SPECIFIER_NOT_SET,
              hash_policy->policy_specifier_case());
    EXPECT_THROW(config(), EnvoyException);
  }
  {
    auto route = route_config_.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
    route->add_hash_policy()->mutable_header()->set_header_name("foo_header");
    route->add_hash_policy()->mutable_connection_properties()->set_source_ip(true);
    auto hash_policy = route->add_hash_policy();
    EXPECT_EQ(envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::
                  POLICY_SPECIFIER_NOT_SET,
              hash_policy->policy_specifier_case());
    EXPECT_THROW(config(), EnvoyException);
  }
}

TEST_F(RouteMatcherTest, ClusterHeader) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster_header: ":authority"
  - match:
      prefix: "/bar"
    route:
      cluster_header: some_header
      timeout: 0s
  )EOF";

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(
      "some_cluster",
      config.route(genHeaders("some_cluster", "/foo", "GET"), 0)->routeEntry()->clusterName());

  EXPECT_EQ(
      "", config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry()->clusterName());

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    headers.addCopy("some_header", "some_cluster");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ("some_cluster", route->routeEntry()->clusterName());

    // Make sure things forward and don't crash.
    // TODO(mattklein123): Make this a real test of behavior.
    EXPECT_EQ(std::chrono::milliseconds(0), route->routeEntry()->timeout());
    route->routeEntry()->finalizeRequestHeaders(headers, stream_info, true);
    route->routeEntry()->priority();
    route->routeEntry()->rateLimitPolicy();
    route->routeEntry()->retryPolicy();
    route->routeEntry()->shadowPolicies();
    route->routeEntry()->virtualCluster(headers);
    route->routeEntry()->virtualHost();
    route->routeEntry()->virtualHost().rateLimitPolicy();
    route->routeEntry()->pathMatchCriterion();
    route->routeEntry()->hedgePolicy();
    route->routeEntry()->maxGrpcTimeout();
    route->routeEntry()->grpcTimeoutOffset();
    route->routeEntry()->upgradeMap();
    route->routeEntry()->internalRedirectPolicy();
  }
}

TEST_F(RouteMatcherTest, ContentType) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
      headers:
      - name: content-type
        exact_match: application/grpc
    route:
      cluster: local_service_grpc
  - match:
      prefix: "/"
    route:
      cluster: local_service
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    EXPECT_EQ("local_service",
              config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("content-type", "application/grpc");
    EXPECT_EQ("local_service_grpc", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("content-type", "foo");
    EXPECT_EQ("local_service", config.route(headers, 0)->routeEntry()->clusterName());
  }
}

TEST_F(RouteMatcherTest, GrpcTimeoutOffset) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: local_service_grpc
  - match:
      prefix: "/"
    route:
      grpc_timeout_offset: 0.01s
      cluster: local_service_grpc
      )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    EXPECT_EQ(
        absl::make_optional(std::chrono::milliseconds(10)),
        config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->grpcTimeoutOffset());
  }
  EXPECT_EQ(absl::nullopt, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                               ->routeEntry()
                               ->grpcTimeoutOffset());
}

TEST_F(RouteMatcherTest, GrpcTimeoutOffsetOfDynamicRoute) {
  // A DynamicRouteEntry will be created when 'cluster_header' is set.
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: local_service_grpc
      max_grpc_timeout: 0.1s
      grpc_timeout_offset: 0.01s
  - match:
      prefix: "/"
    route:
      max_grpc_timeout: 0.2s
      grpc_timeout_offset: 0.02s
      cluster_header: request_to
      )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl reqeust_headers = genHeaders("www.lyft.com", "/", "GET");
    reqeust_headers.addCopy(Http::LowerCaseString("reqeust_to"), "dynamic_grpc_service");
    EXPECT_EQ(absl::make_optional(std::chrono::milliseconds(20)),
              config.route(reqeust_headers, 0)->routeEntry()->grpcTimeoutOffset());
    EXPECT_EQ(absl::make_optional(std::chrono::milliseconds(200)),
              config.route(reqeust_headers, 0)->routeEntry()->maxGrpcTimeout());
  }
  {

    EXPECT_EQ(absl::make_optional(std::chrono::milliseconds(10)),
              config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                  ->routeEntry()
                  ->grpcTimeoutOffset());
    EXPECT_EQ(
        absl::make_optional(std::chrono::milliseconds(100)),
        config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)->routeEntry()->maxGrpcTimeout());
  }
}

TEST_F(RouteMatcherTest, FractionalRuntime) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - match:
          prefix: "/"
          runtime_fraction:
            default_value:
              numerator: 50
              denominator: MILLION
            runtime_key: "bogus_key"
        route:
          cluster: "something_else"
      - match:
          prefix: "/"
        route:
          cluster: "www2"
  )EOF";

  Runtime::MockSnapshot snapshot;
  ON_CALL(factory_context_.runtime_loader_, snapshot()).WillByDefault(ReturnRef(snapshot));

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, false);

  EXPECT_CALL(snapshot, featureEnabled("bogus_key",
                                       Matcher<const envoy::type::v3::FractionalPercent&>(_), 41))
      .WillRepeatedly(Return(true));
  EXPECT_EQ(
      "something_else",
      config.route(genHeaders("www.lyft.com", "/foo", "GET"), 41)->routeEntry()->clusterName());

  EXPECT_CALL(snapshot, featureEnabled("bogus_key",
                                       Matcher<const envoy::type::v3::FractionalPercent&>(_), 43))
      .WillRepeatedly(Return(false));
  EXPECT_EQ(
      "www2",
      config.route(genHeaders("www.lyft.com", "/foo", "GET"), 43)->routeEntry()->clusterName());
}

TEST_F(RouteMatcherTest, ShadowClusterNotFound) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      request_mirror_policy:
        cluster: some_cluster
      cluster: www2
  )EOF";

  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("www2")))
      .WillRepeatedly(Return(&factory_context_.cluster_manager_.thread_local_cluster_));
  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("some_cluster")))
      .WillRepeatedly(Return(nullptr));

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, ClusterNotFound) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: www2
  )EOF";

  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("www2"))).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, ClusterNotFoundNotChecking) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: www2
  )EOF";

  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("www2"))).WillRepeatedly(Return(nullptr));

  TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, false);
}

TEST_F(RouteMatcherTest, ClusterNotFoundNotCheckingViaConfig) {
  const std::string yaml = R"EOF(
validate_clusters: false
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: www
  )EOF";

  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("www2"))).WillRepeatedly(Return(nullptr));

  TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
}

TEST_F(RouteMatcherTest, AttemptCountHeader) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    include_request_attempt_count: true
    include_attempt_count_in_response: true
    routes:
      - match: { prefix: "/"}
        route:
          cluster: "whatever"
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_TRUE(config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                  ->routeEntry()
                  ->includeAttemptCountInRequest());

  EXPECT_TRUE(config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                  ->routeEntry()
                  ->includeAttemptCountInResponse());
}

TEST_F(RouteMatcherTest, ClusterNotFoundResponseCode) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/"}
        route:
          cluster: "not_found"
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, false);

  Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");

  EXPECT_EQ("not_found", config.route(headers, 0)->routeEntry()->clusterName());
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            config.route(headers, 0)->routeEntry()->clusterNotFoundResponseCode());
}

TEST_F(RouteMatcherTest, ClusterNotFoundResponseCodeConfig503) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/"}
        route:
          cluster: "not_found"
          cluster_not_found_response_code: SERVICE_UNAVAILABLE
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, false);

  Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");

  EXPECT_EQ("not_found", config.route(headers, 0)->routeEntry()->clusterName());
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            config.route(headers, 0)->routeEntry()->clusterNotFoundResponseCode());
}

TEST_F(RouteMatcherTest, ClusterNotFoundResponseCodeConfig404) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/"}
        route:
          cluster: "not_found"
          cluster_not_found_response_code: NOT_FOUND
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, false);

  Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");

  EXPECT_EQ("not_found", config.route(headers, 0)->routeEntry()->clusterName());
  EXPECT_EQ(Http::Code::NotFound,
            config.route(headers, 0)->routeEntry()->clusterNotFoundResponseCode());
}

// TODO(dereka) DEPRECATED_FEATURE_TEST can be removed when `request_mirror_policy` is removed.
TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(Shadow)) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      request_mirror_policy:
        cluster: some_cluster
      cluster: www2
  - match:
      prefix: "/bar"
    route:
      request_mirror_policy:
        cluster: some_cluster2
        runtime_fraction:
          default_value:
            numerator: 20
            denominator: HUNDRED
          runtime_key: foo
      cluster: www2
  - match:
      prefix: "/baz"
    route:
      cluster: www2
  - match:
      prefix: "/boz"
    route:
      request_mirror_policies:
        - cluster: some_cluster
        - cluster: some_cluster2
          runtime_fraction:
            default_value:
              numerator: 20
              denominator: HUNDRED
            runtime_key: foo
      cluster: www2
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  const auto& foo_shadow_policies =
      config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)->routeEntry()->shadowPolicies();
  EXPECT_EQ(1, foo_shadow_policies.size());
  EXPECT_EQ("some_cluster", foo_shadow_policies[0]->cluster());
  EXPECT_EQ("", foo_shadow_policies[0]->runtimeKey());

  const auto& bar_shadow_policies =
      config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry()->shadowPolicies();
  EXPECT_EQ(1, bar_shadow_policies.size());
  EXPECT_EQ("some_cluster2", bar_shadow_policies[0]->cluster());
  EXPECT_EQ("foo", bar_shadow_policies[0]->runtimeKey());

  EXPECT_EQ(0, config.route(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                   ->routeEntry()
                   ->shadowPolicies()
                   .size());

  const auto& boz_shadow_policies =
      config.route(genHeaders("www.lyft.com", "/boz", "GET"), 0)->routeEntry()->shadowPolicies();
  EXPECT_EQ(2, boz_shadow_policies.size());
  EXPECT_EQ("some_cluster", boz_shadow_policies[0]->cluster());
  EXPECT_EQ("", boz_shadow_policies[0]->runtimeKey());
  EXPECT_EQ("some_cluster2", boz_shadow_policies[1]->cluster());
  EXPECT_EQ("foo", boz_shadow_policies[1]->runtimeKey());
}

TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(ShadowPolicyAndPolicies)) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      request_mirror_policy:
        cluster: some_cluster
      request_mirror_policies:
      - cluster: some_other_cluster
      cluster: www2
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Cannot specify both request_mirror_policy and request_mirror_policies");
}

class RouteConfigurationV2 : public testing::Test, public ConfigImplTestBase {};

// When removing runtime_key: this test can be removed.
TEST_F(RouteConfigurationV2, DEPRECATED_FEATURE_TEST(RequestMirrorPolicy)) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: mirror
    domains: [mirror.lyft.com]
    routes:
      - match: { prefix: "/"}
        route:
          cluster: foo
          request_mirror_policy:
            cluster: foo_mirror
            runtime_key: will_be_ignored
            runtime_fraction:
               default_value:
                 numerator: 20
                 denominator: HUNDRED
               runtime_key: mirror_key

  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ("foo_mirror", config.route(genHeaders("mirror.lyft.com", "/foo", "GET"), 0)
                              ->routeEntry()
                              ->shadowPolicies()[0]
                              ->cluster());

  // `runtime_fraction` takes precedence over the deprecated `runtime_key` field.
  EXPECT_EQ("mirror_key", config.route(genHeaders("mirror.lyft.com", "/foo", "GET"), 0)
                              ->routeEntry()
                              ->shadowPolicies()[0]
                              ->runtimeKey());

  const auto& default_value = config.route(genHeaders("mirror.lyft.com", "/foo", "GET"), 0)
                                  ->routeEntry()
                                  ->shadowPolicies()[0]
                                  ->defaultValue();
  EXPECT_EQ(20, default_value.numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::HUNDRED, default_value.denominator());
}

TEST_F(RouteMatcherTest, Retry) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: www2
      retry_policy:
        retry_on: connect-failure
  - match:
      prefix: "/bar"
    route:
      cluster: www2
  - match:
      prefix: "/"
    route:
      cluster: www2
      retry_policy:
        per_try_timeout: 1s
        num_retries: 3
        retry_on: 5xx,gateway-error,connect-failure,reset
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(1U, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(1, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                   ->routeEntry()
                   ->retryPolicy()
                   .numRetries());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(1000),
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(3U, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE | RetryPolicy::RETRY_ON_5XX |
                RetryPolicy::RETRY_ON_GATEWAY_ERROR | RetryPolicy::RETRY_ON_RESET,
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
}

TEST_F(RouteMatcherTest, RetryVirtualHostLevel) {
  const std::string yaml = R"EOF(
virtual_hosts:
- domains: [www.lyft.com]
  per_request_buffer_limit_bytes: 8
  name: www
  retry_policy: {num_retries: 3, per_try_timeout: 1s, retry_on: '5xx,gateway-error,connect-failure,reset'}
  routes:
  - match: {prefix: /foo}
    per_request_buffer_limit_bytes: 7
    route:
      cluster: www
      retry_policy: {retry_on: connect-failure}
  - match: {prefix: /bar}
    route: {cluster: www}
  - match: {prefix: /}
    route: {cluster: www}
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // Route level retry policy takes precedence.
  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(1U, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
  EXPECT_EQ(7U, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->retryShadowBufferLimit());

  // Virtual Host level retry policy kicks in.
  EXPECT_EQ(std::chrono::milliseconds(1000),
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(3U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE | RetryPolicy::RETRY_ON_5XX |
                RetryPolicy::RETRY_ON_GATEWAY_ERROR | RetryPolicy::RETRY_ON_RESET,
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
  EXPECT_EQ(std::chrono::milliseconds(1000),
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(3U, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE | RetryPolicy::RETRY_ON_5XX |
                RetryPolicy::RETRY_ON_GATEWAY_ERROR | RetryPolicy::RETRY_ON_RESET,
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
  EXPECT_EQ(8U, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                    ->routeEntry()
                    ->retryShadowBufferLimit());
}

TEST_F(RouteMatcherTest, GrpcRetry) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: www2
      retry_policy:
        retry_on: connect-failure
  - match:
      prefix: "/bar"
    route:
      cluster: www2
  - match:
      prefix: "/"
    route:
      cluster: www2
      retry_policy:
        per_try_timeout: 1s
        num_retries: 3
        retry_on: 5xx,deadline-exceeded,resource-exhausted
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(1U, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(1, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                   ->routeEntry()
                   ->retryPolicy()
                   .numRetries());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(1000),
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(3U, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_5XX | RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED |
                RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED,
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
}

// Test route-specific retry back-off intervals.
TEST_F(RouteMatcherTest, RetryBackOffIntervals) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: www2
      retry_policy:
        retry_back_off:
          base_interval: 0.050s
  - match:
      prefix: "/bar"
    route:
      cluster: www2
      retry_policy:
        retry_back_off:
          base_interval: 0.100s
          max_interval: 0.500s
  - match:
      prefix: "/baz"
    route:
      cluster: www2
      retry_policy:
        retry_back_off:
          base_interval: 0.0001s # < 1 ms
          max_interval: 0.0001s
  - match:
      prefix: "/"
    route:
      cluster: www2
      retry_policy:
        retry_on: connect-failure
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(50),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .baseInterval());

  EXPECT_EQ(absl::nullopt, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                               ->routeEntry()
                               ->retryPolicy()
                               .maxInterval());

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(100),
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .baseInterval());

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(500),
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .maxInterval());

  // Sub-millisecond interval converted to 1 ms.
  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(1),
            config.route(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .baseInterval());

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(1),
            config.route(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .maxInterval());

  EXPECT_EQ(absl::nullopt, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                               ->routeEntry()
                               ->retryPolicy()
                               .baseInterval());

  EXPECT_EQ(absl::nullopt, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                               ->routeEntry()
                               ->retryPolicy()
                               .maxInterval());
}

// Test invalid route-specific retry back-off configs.
TEST_F(RouteMatcherTest, InvalidRetryBackOff) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: backoff
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route:
          cluster: backoff
          retry_policy:
            retry_back_off:
              base_interval: 10s
              max_interval: 5s
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "retry_policy.max_interval must greater than or equal to the base_interval");
}

TEST_F(RouteMatcherTest, HedgeRouteLevel) {
  const std::string yaml = R"EOF(
virtual_hosts:
- domains: [www.lyft.com]
  name: www
  routes:
  - match: {prefix: /foo}
    route:
      cluster: www
      hedge_policy:
        initial_requests: 3
        additional_request_chance:
          numerator: 4
          denominator: HUNDRED
  - match: {prefix: /bar}
    route: {cluster: www}
  - match: {prefix: /}
    route:
      cluster: www
      hedge_policy:
        hedge_on_per_try_timeout: true
        initial_requests: 5
        additional_request_chance:
          numerator: 40
          denominator: HUNDRED
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(3, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                   ->routeEntry()
                   ->hedgePolicy()
                   .initialRequests());
  EXPECT_EQ(false, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                       ->routeEntry()
                       ->hedgePolicy()
                       .hedgeOnPerTryTimeout());
  envoy::type::v3::FractionalPercent percent =
      config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
          ->routeEntry()
          ->hedgePolicy()
          .additionalRequestChance();
  EXPECT_EQ(4, percent.numerator());
  EXPECT_EQ(100, ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent.denominator()));

  EXPECT_EQ(1, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                   ->routeEntry()
                   ->hedgePolicy()
                   .initialRequests());
  EXPECT_EQ(false, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                       ->routeEntry()
                       ->hedgePolicy()
                       .hedgeOnPerTryTimeout());
  percent = config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->hedgePolicy()
                .additionalRequestChance();
  EXPECT_EQ(0, percent.numerator());

  EXPECT_EQ(5, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                   ->routeEntry()
                   ->hedgePolicy()
                   .initialRequests());
  EXPECT_EQ(true, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                      ->routeEntry()
                      ->hedgePolicy()
                      .hedgeOnPerTryTimeout());
  percent = config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->hedgePolicy()
                .additionalRequestChance();
  EXPECT_EQ(40, percent.numerator());
  EXPECT_EQ(100, ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent.denominator()));
}

TEST_F(RouteMatcherTest, HedgeVirtualHostLevel) {
  const std::string yaml = R"EOF(
virtual_hosts:
- domains: [www.lyft.com]
  name: www
  hedge_policy: {initial_requests: 3}
  routes:
  - match: {prefix: /foo}
    route:
      cluster: www
      hedge_policy: {hedge_on_per_try_timeout: true}
  - match: {prefix: /bar}
    route:
      hedge_policy: {additional_request_chance: {numerator: 30, denominator: HUNDRED}}
      cluster: www
  - match: {prefix: /}
    route: {cluster: www}
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // Route level hedge policy takes precedence.
  EXPECT_EQ(1, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                   ->routeEntry()
                   ->hedgePolicy()
                   .initialRequests());
  EXPECT_EQ(true, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                      ->routeEntry()
                      ->hedgePolicy()
                      .hedgeOnPerTryTimeout());
  envoy::type::v3::FractionalPercent percent =
      config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
          ->routeEntry()
          ->hedgePolicy()
          .additionalRequestChance();
  EXPECT_EQ(0, percent.numerator());

  // Virtual Host level hedge policy kicks in.
  EXPECT_EQ(1, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                   ->routeEntry()
                   ->hedgePolicy()
                   .initialRequests());
  EXPECT_EQ(false, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                       ->routeEntry()
                       ->hedgePolicy()
                       .hedgeOnPerTryTimeout());
  percent = config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                ->routeEntry()
                ->hedgePolicy()
                .additionalRequestChance();
  EXPECT_EQ(30, percent.numerator());
  EXPECT_EQ(100, ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent.denominator()));

  EXPECT_EQ(3, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                   ->routeEntry()
                   ->hedgePolicy()
                   .initialRequests());
  EXPECT_EQ(false, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                       ->routeEntry()
                       ->hedgePolicy()
                       .hedgeOnPerTryTimeout());
  percent = config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->hedgePolicy()
                .additionalRequestChance();
  EXPECT_EQ(0, percent.numerator());
}

TEST_F(RouteMatcherTest, TestBadDefaultConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
- name: www2_staging
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2_staging
internal_only_headers:
- x-lyft-user-id
  )EOF";

  EXPECT_THROW(TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, TestDuplicateDomainConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
- name: www2_staging
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2_staging
  )EOF";

  EXPECT_THROW(TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

// Test to detect if hostname matches are case-insensitive
TEST_F(RouteMatcherTest, TestCaseSensitiveDomainConfig) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: [www.lyft.com]
    routes:
      - match: { prefix: "/" }
        route: { cluster: www2 }
  - name: www2_staging
    domains: [www.LYFt.cOM]
    routes:
      - match: { prefix: "/" }
        route: { cluster: www2_staging }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Only unique values for domains are permitted. Duplicate entry of domain www.lyft.com");
}

TEST_F(RouteMatcherTest, TestDuplicateWildcardDomainConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains: ["*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: www2 }
- name: www2_staging
  domains: ["*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: www2_staging }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Only a single wildcard domain is permitted");
}

TEST_F(RouteMatcherTest, TestDuplicateSuffixWildcardDomainConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains: ["*.lyft.com"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: www2 }
- name: www2_staging
  domains: ["*.LYFT.COM"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: www2_staging }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Only unique values for domains are permitted. Duplicate entry of domain *.lyft.com");
}

TEST_F(RouteMatcherTest, TestDuplicatePrefixWildcardDomainConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains: ["bar.*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: www2 }
- name: www2_staging
  domains: ["BAR.*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: www2_staging }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Only unique values for domains are permitted. Duplicate entry of domain bar.*");
}

TEST_F(RouteMatcherTest, TestInvalidCharactersInPrefixRewrites) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www
  domains: ["*"]
  routes:
  - match: { prefix: "/foo" }
    route:
      prefix_rewrite: "/\ndroptable"
      cluster: www
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "RouteActionValidationError.PrefixRewrite:.*value does not match regex pattern");
}

TEST_F(RouteMatcherTest, TestInvalidCharactersInHostRewrites) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www
  domains: ["*"]
  routes:
  - match: { prefix: "/foo" }
    route:
      host_rewrite: "new_host\ndroptable"
      cluster: www
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "RouteActionValidationError.HostRewriteLiteral:.*value does not match regex pattern");
}

TEST_F(RouteMatcherTest, TestInvalidCharactersInAutoHostRewrites) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www
  domains: ["*"]
  routes:
  - match: { prefix: "/foo" }
    route:
      auto_host_rewrite_header: "x-host\ndroptable"
      cluster: www
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "RouteActionValidationError.HostRewriteHeader:.*value does not match regex pattern");
}

TEST_F(RouteMatcherTest, TestInvalidCharactersInHostRedirect) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www
  domains: ["*"]
  routes:
  - match: { prefix: "/foo" }
    redirect: { host_redirect: "new.host\ndroptable" }
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "RedirectActionValidationError.HostRedirect:.*value does not match regex pattern");
}

TEST_F(RouteMatcherTest, TestInvalidCharactersInPathRedirect) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www
  domains: ["*"]
  routes:
  - match: { prefix: "/foo" }
    redirect: { path_redirect: "/new_path\ndroptable" }
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "RedirectActionValidationError.PathRedirect:.*value does not match regex pattern");
}

TEST_F(RouteMatcherTest, TestInvalidCharactersInPrefixRewriteRedirect) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www
  domains: ["*"]
  routes:
  - match: { prefix: "/foo" }
    redirect: { prefix_rewrite: "/new/prefix\ndroptable"}
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "RedirectActionValidationError.PrefixRewrite:.*value does not match regex pattern");
}

TEST_F(RouteMatcherTest, TestPrefixAndRegexRewrites) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains: ["bar.*"]
  routes:
  - match: { prefix: "/foo" }
    route:
      prefix_rewrite: /
      regex_rewrite:
        pattern:
          google_re2: {}
          regex: foo
        substitution: bar
      cluster: www2
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Cannot specify both prefix_rewrite and regex_rewrite");
}

TEST_F(RouteMatcherTest, TestDomainMatchOrderConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: exact
  domains: ["www.example.com", "www.example.cc", "wwww.example.com" ]
  routes:
  - match: { prefix: "/" }
    route: { cluster: exact }
- name: suffix
  domains: ["*w.example.com" ]
  routes:
  - match: { prefix: "/" }
    route: { cluster: suffix }
- name: prefix
  domains: ["www.example.c*", "ww.example.c*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: prefix }
- name: default
  domains: ["*"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: default }
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(
      "exact",
      config.route(genHeaders("www.example.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "exact",
      config.route(genHeaders("wwww.example.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("exact",
            config.route(genHeaders("www.example.cc", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("suffix",
            config.route(genHeaders("ww.example.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("prefix",
            config.route(genHeaders("www.example.co", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("default",
            config.route(genHeaders("w.example.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("default",
            config.route(genHeaders("www.example.c", "/", "GET"), 0)->routeEntry()->clusterName());
}

TEST_F(RouteMatcherTest, NoProtocolInHeadersWhenTlsIsRequired) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www
  require_tls: all
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  // route may be called early in some edge cases and "x-forwarded-proto" will not be set.
  Http::TestRequestHeaderMapImpl headers{{":authority", "www.lyft.com"}, {":path", "/"}};
  EXPECT_EQ(nullptr, config.route(headers, 0));
}

/**
 * @brief  Generate headers for testing
 * @param ssl set true to insert "x-forwarded-proto: https", else "x-forwarded-proto: http"
 * @param internal nullopt for no such "x-envoy-internal" header, or explicit "true/false"
 * @return Http::TestRequestHeaderMapImpl
 */
static Http::TestRequestHeaderMapImpl genRedirectHeaders(const std::string& host,
                                                         const std::string& path, bool ssl,
                                                         absl::optional<bool> internal) {
  Http::TestRequestHeaderMapImpl headers{
      {":authority", host}, {":path", path}, {"x-forwarded-proto", ssl ? "https" : "http"}};
  if (internal.has_value()) {
    headers.addCopy("x-envoy-internal", internal.value() ? "true" : "false");
  }

  return headers;
}

TEST_F(RouteMatcherTest, RouteName) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - name: "route-test"
        match: { prefix: "/"}
        route:
          cluster: "ufesservice"
  - name: redirect
    domains: [redirect.lyft.com]
    routes:
      - name: "route-test-2"
        match: { path: /host }
        redirect: { host_redirect: new.lyft.com }
  )EOF";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context, false);
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    EXPECT_EQ("route-test", config.route(headers, 0)->routeEntry()->routeName());
  }

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    EXPECT_EQ("route-test-2", redirect->routeName());
  }
}

TEST_F(RouteMatcherTest, DirectResponse) {
  const auto pathname =
      TestEnvironment::writeStringToFileForTest("direct_response_body", "Example text 3");

  static const std::string yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: [www.lyft.com]
    require_tls: all
    routes:
      - match: { prefix: "/" }
        route: { cluster: www2 }
  - name: api
    domains: [api.lyft.com]
    require_tls: external_only
    routes:
      - match: { prefix: "/" }
        route: { cluster: www2 }
  - name: redirect
    domains: [redirect.lyft.com]
    routes:
      - match: { path: /host }
        redirect: { host_redirect: new.lyft.com }
      - match: { path: /path }
        redirect: { path_redirect: /new_path }
      - match: { path: /https }
        redirect: { https_redirect: true }
      - match: { path: /host_path }
        redirect: { host_redirect: new.lyft.com, path_redirect: /new_path }
      - match: { path: /host_https }
        redirect: { host_redirect: new.lyft.com, https_redirect: true }
      - match: { path: /path_https }
        redirect: { path_redirect: /new_path, https_redirect: true }
      - match: { path: /host_path_https }
        redirect: { host_redirect: new.lyft.com, path_redirect: /new_path, https_redirect: true }
      - match: { path: /port }
        redirect: { port_redirect: 8080 }
      - match: { path: /host_port }
        redirect: { host_redirect: new.lyft.com, port_redirect: 8080 }
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: new.lyft.com, port_redirect: 8080 }
  - name: redirect_domain_port_80
    domains: [redirect.lyft.com:80]
    routes:
      - match: { path: /ws }
        redirect: { scheme_redirect: ws }
      - match: { path: /host_path_https }
        redirect: { host_redirect: new.lyft.com, path_redirect: /new_path, https_redirect: true }
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: new.lyft.com, port_redirect: 8080 }
  - name: redirect_domain_port_443
    domains: [redirect.lyft.com:443]
    routes:
      - match: { path: /ws }
        redirect: { scheme_redirect: ws }
      - match: { path: /host_path_http }
        redirect: { scheme_redirect: http, host_redirect: new.lyft.com, path_redirect: /new_path}
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: new.lyft.com, port_redirect: 8080 }
  - name: redirect_domain_port_8080
    domains: [redirect.lyft.com:8080]
    routes:
      - match: { path: /port }
        redirect: { port_redirect: 8181 }
  - name: redirect_ipv4
    domains: [10.0.0.1]
    routes:
      - match: { path: /port }
        redirect: { port_redirect: 8080 }
      - match: { path: /host_port }
        redirect: { host_redirect: 20.0.0.2, port_redirect: 8080 }
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: 20.0.0.2, port_redirect: 8080 }
  - name: redirect_ipv4_port_8080
    domains: [10.0.0.1:8080]
    routes:
      - match: { path: /port }
        redirect: { port_redirect: 8181 }
  - name: redirect_ipv4_port_80
    domains: [10.0.0.1:80]
    routes:
      - match: { path: /ws }
        redirect: { scheme_redirect: ws }
      - match: { path: /host_path_https }
        redirect: { host_redirect: 20.0.0.2, path_redirect: /new_path, https_redirect: true }
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: 20.0.0.2, port_redirect: 8080 }
  - name: redirect_ipv4_port_443
    domains: [10.0.0.1:443]
    routes:
      - match: { path: /ws }
        redirect: { scheme_redirect: ws }
      - match: { path: /host_path_http }
        redirect: { scheme_redirect: http, host_redirect: 20.0.0.2, path_redirect: /new_path}
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: 20.0.0.2, port_redirect: 8080 }
  - name: redirect_ipv6
    domains: ["[fe80::1]"]
    routes:
      - match: { path: /port }
        redirect: { port_redirect: 8080 }
      - match: { path: /host_port }
        redirect: { host_redirect: "[fe80::2]", port_redirect: 8080 }
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: "[fe80::2]", port_redirect: 8080 }
  - name: redirect_ipv6_port_8080
    domains: ["[fe80::1]:8080"]
    routes:
      - match: { path: /port }
        redirect: { port_redirect: 8181 }
  - name: redirect_ipv6_port_80
    domains: ["[fe80::1]:80"]
    routes:
      - match: { path: /ws }
        redirect: { scheme_redirect: ws }
      - match: { path: /host_path_https }
        redirect: { host_redirect: "[fe80::2]", path_redirect: /new_path, https_redirect: true }
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: "[fe80::2]", port_redirect: 8080 }
  - name: redirect_ipv6_port_443
    domains: ["[fe80::1]:443"]
    routes:
      - match: { path: /ws }
        redirect: { scheme_redirect: ws }
      - match: { path: /host_path_http }
        redirect: { scheme_redirect: http, host_redirect: "[fe80::2]", path_redirect: /new_path}
      - match: { path: /scheme_host_port }
        redirect: { scheme_redirect: ws, host_redirect: "[fe80::2]", port_redirect: 8080 }
  - name: direct
    domains: [direct.example.com]
    routes:
    - match: { prefix: /gone }
      direct_response:
        status: 410
        body: { inline_bytes: "RXhhbXBsZSB0ZXh0IDE=" }
    - match: { prefix: /error }
      direct_response:
        status: 500
        body: { inline_string: "Example text 2" }
    - match: { prefix: /no_body }
      direct_response:
        status: 200
    - match: { prefix: /static }
      direct_response:
        status: 200
        body: { filename: )EOF" + pathname +
                                  R"EOF(}
    - match: { prefix: / }
      route: { cluster: www2 }
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("www.lyft.com", "/foo", false, false);
    EXPECT_EQ("https://www.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->decorator());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("api.lyft.com", "/foo", false, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("api.lyft.com", "/foo", false, false);
    EXPECT_EQ("https://api.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "api.lyft.com", "/foo", false, absl::nullopt /* no x-envoy-internal header */);
    EXPECT_EQ("https://api.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host", false, false);
    EXPECT_EQ("http://new.lyft.com/host",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host_path", true, false);
    EXPECT_EQ("https://new.lyft.com/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("direct.example.com", "/gone", true, false);
    EXPECT_EQ(Http::Code::Gone, config.route(headers, 0)->directResponseEntry()->responseCode());
    EXPECT_EQ("Example text 1", config.route(headers, 0)->directResponseEntry()->responseBody());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("direct.example.com", "/error", true, false);
    EXPECT_EQ(Http::Code::InternalServerError,
              config.route(headers, 0)->directResponseEntry()->responseCode());
    EXPECT_EQ("Example text 2", config.route(headers, 0)->directResponseEntry()->responseBody());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("direct.example.com", "/no_body", true, false);
    EXPECT_EQ(Http::Code::OK, config.route(headers, 0)->directResponseEntry()->responseCode());
    EXPECT_TRUE(config.route(headers, 0)->directResponseEntry()->responseBody().empty());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("direct.example.com", "/static", true, false);
    EXPECT_EQ(Http::Code::OK, config.route(headers, 0)->directResponseEntry()->responseCode());
    EXPECT_EQ("Example text 3", config.route(headers, 0)->directResponseEntry()->responseBody());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("direct.example.com", "/other", true, false);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/https", false, false);
    EXPECT_EQ("https://redirect.lyft.com/https",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->perFilterConfig("bar"));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host_https", false, false);
    EXPECT_EQ("https://new.lyft.com/host_https",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path_https", false, false);
    EXPECT_EQ("https://redirect.lyft.com/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host_path_https", false, false);
    EXPECT_EQ("https://new.lyft.com/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/port", false, false);
    EXPECT_EQ("http://redirect.lyft.com:8080/port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com:8080", "/port", false, false);
    EXPECT_EQ("http://redirect.lyft.com:8181/port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host_port", false, false);
    EXPECT_EQ("http://new.lyft.com:8080/host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/scheme_host_port", false, false);
    EXPECT_EQ("ws://new.lyft.com:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com:80", "/ws", true, false);
    EXPECT_EQ("ws://redirect.lyft.com:80/ws",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com:80", "/host_path_https", false, false);
    EXPECT_EQ("https://new.lyft.com/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com:80", "/scheme_host_port", false, false);
    EXPECT_EQ("ws://new.lyft.com:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com:443", "/ws", false, false);
    EXPECT_EQ("ws://redirect.lyft.com:443/ws",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com:443", "/host_path_http", true, false);
    EXPECT_EQ("http://new.lyft.com/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com:443", "/scheme_host_port", true, false);
    EXPECT_EQ("ws://new.lyft.com:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders("10.0.0.1", "/port", false, false);
    EXPECT_EQ("http://10.0.0.1:8080/port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1:8080", "/port", false, false);
    EXPECT_EQ("http://10.0.0.1:8181/port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1", "/host_port", false, false);
    EXPECT_EQ("http://20.0.0.2:8080/host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1", "/scheme_host_port", false, false);
    EXPECT_EQ("ws://20.0.0.2:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders("10.0.0.1:80", "/ws", true, false);
    EXPECT_EQ("ws://10.0.0.1:80/ws",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1:80", "/host_path_https", false, false);
    EXPECT_EQ("https://20.0.0.2/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1:80", "/scheme_host_port", false, false);
    EXPECT_EQ("ws://20.0.0.2:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1:443", "/ws", false, false);
    EXPECT_EQ("ws://10.0.0.1:443/ws",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1:443", "/host_path_http", true, false);
    EXPECT_EQ("http://20.0.0.2/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("10.0.0.1:443", "/scheme_host_port", true, false);
    EXPECT_EQ("ws://20.0.0.2:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders("[fe80::1]", "/port", false, false);

    EXPECT_EQ("http://[fe80::1]:8080/port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]:8080", "/port", false, false);
    EXPECT_EQ("http://[fe80::1]:8181/port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]", "/host_port", false, false);
    EXPECT_EQ("http://[fe80::2]:8080/host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]", "/scheme_host_port", false, false);
    EXPECT_EQ("ws://[fe80::2]:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders("[fe80::1]:80", "/ws", true, false);
    EXPECT_EQ("ws://[fe80::1]:80/ws",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]:80", "/host_path_https", false, false);
    EXPECT_EQ("https://[fe80::2]/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]:80", "/scheme_host_port", false, false);
    EXPECT_EQ("ws://[fe80::2]:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]:443", "/ws", false, false);
    EXPECT_EQ("ws://[fe80::1]:443/ws",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]:443", "/host_path_http", true, false);
    EXPECT_EQ("http://[fe80::2]/new_path",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("[fe80::1]:443", "/scheme_host_port", true, false);
    EXPECT_EQ("ws://[fe80::2]:8080/scheme_host_port",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
}

TEST_F(RouteMatcherTest, ExclusiveRouteEntryOrDirectResponseEntry) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
- name: redirect
  domains:
  - redirect.lyft.com
  routes:
  - match:
      prefix: "/foo"
    redirect:
      host_redirect: new.lyft.com
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
    EXPECT_EQ("www2", config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->routeEntry());
  }
}

TEST_F(RouteMatcherTest, ExclusiveWeightedClustersEntryOrDirectResponseEntry) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      weighted_clusters:
        clusters:
        - name: www2
          weight: 100
- name: redirect
  domains:
  - redirect.lyft.com
  routes:
  - match:
      prefix: "/foo"
    redirect:
      host_redirect: new.lyft.com
  - match:
      prefix: "/foo1"
    redirect:
      host_redirect: "[fe80::1]"
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
    EXPECT_EQ("www2", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->routeEntry());
  }
}

struct Foo : public Envoy::Config::TypedMetadata::Object {};
struct Baz : public Envoy::Config::TypedMetadata::Object {
  Baz(std::string n) : name(n) {}
  std::string name;
};
class BazFactory : public HttpRouteTypedMetadataFactory {
public:
  std::string name() const override { return "baz"; }
  // Returns nullptr (conversion failure) if d is empty.
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Struct& d) const override {
    if (d.fields().find("name") != d.fields().end()) {
      return std::make_unique<Baz>(d.fields().at("name").string_value());
    }
    throw EnvoyException("Cannot create a Baz when metadata is empty.");
  }
};

TEST_F(RouteMatcherTest, WeightedClusters) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: www1
    domains: ["www1.lyft.com"]
    routes:
      - match: { prefix: "/" }
        metadata: { filter_metadata: { com.bar.foo: { baz: test_value }, baz: {name: meh} } }
        decorator:
          operation: hello
        route:
          weighted_clusters:
            clusters:
              - name: cluster1
                weight: 30
              - name: cluster2
                weight: 30
              - name: cluster3
                weight: 40
  - name: www2
    domains: ["www2.lyft.com"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            clusters:
              - name: cluster1
                weight: 2000
              - name: cluster2
                weight: 3000
              - name: cluster3
                weight: 5000
            total_weight: 10000
  - name: www3
    domains: ["www3.lyft.com"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            runtime_key_prefix: www3_weights
            clusters:
              - name: cluster1
                weight: 30
              - name: cluster2
                weight: 30
              - name: cluster3
                weight: 40
  - name: www4
    domains: ["www4.lyft.com"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            runtime_key_prefix: www4_weights
            clusters:
              - name: cluster1
                weight: 2000
              - name: cluster2
                weight: 3000
              - name: cluster3
                weight: 5000
            total_weight: 10000
  )EOF";

  BazFactory baz_factory;
  Registry::InjectFactory<HttpRouteTypedMetadataFactory> registered_factory(baz_factory);
  auto& runtime = factory_context_.runtime_loader_;
  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("www1.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
  }

  // Weighted Cluster with no runtime, default total weight
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www1.lyft.com", "/foo", "GET");
    EXPECT_EQ("cluster1", config.route(headers, 115)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 445)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 560)->routeEntry()->clusterName());
  }

  // Make sure weighted cluster entries call through to the parent when needed.
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www1.lyft.com", "/foo", "GET");
    auto route = config.route(headers, 115);
    const RouteEntry* route_entry = route->routeEntry();
    EXPECT_EQ(nullptr, route_entry->hashPolicy());
    EXPECT_TRUE(route_entry->opaqueConfig().empty());
    EXPECT_FALSE(route_entry->autoHostRewrite());
    EXPECT_TRUE(route_entry->includeVirtualHostRateLimits());
    EXPECT_EQ(Http::Code::ServiceUnavailable, route_entry->clusterNotFoundResponseCode());
    EXPECT_EQ(nullptr, route_entry->corsPolicy());
    EXPECT_EQ("test_value",
              Envoy::Config::Metadata::metadataValue(&route_entry->metadata(), "com.bar.foo", "baz")
                  .string_value());
    EXPECT_EQ(nullptr, route_entry->typedMetadata().get<Foo>(baz_factory.name()));
    EXPECT_EQ("meh", route_entry->typedMetadata().get<Baz>(baz_factory.name())->name);
    EXPECT_EQ("hello", route->decorator()->getOperation());

    Http::TestResponseHeaderMapImpl response_headers;
    StreamInfo::MockStreamInfo stream_info;
    route_entry->finalizeResponseHeaders(response_headers, stream_info);
    EXPECT_EQ(response_headers, Http::TestResponseHeaderMapImpl{});
  }

  // Weighted Cluster with no runtime, total weight = 10000
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www2.lyft.com", "/foo", "GET");
    EXPECT_EQ("cluster1", config.route(headers, 1150)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 4500)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 8900)->routeEntry()->clusterName());
  }

  // Weighted Cluster with valid runtime values, default total weight
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www3.lyft.com", "/foo", "GET");
    EXPECT_CALL(runtime.snapshot_, featureEnabled("www3", 100, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(runtime.snapshot_, getInteger("www3_weights.cluster1", 30))
        .WillRepeatedly(Return(80));
    EXPECT_CALL(runtime.snapshot_, getInteger("www3_weights.cluster2", 30))
        .WillRepeatedly(Return(10));
    EXPECT_CALL(runtime.snapshot_, getInteger("www3_weights.cluster3", 40))
        .WillRepeatedly(Return(10));

    EXPECT_EQ("cluster1", config.route(headers, 45)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 82)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 92)->routeEntry()->clusterName());
  }

  // Weighted Cluster with invalid runtime values, default total weight
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www3.lyft.com", "/foo", "GET");
    EXPECT_CALL(runtime.snapshot_, featureEnabled("www3", 100, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(runtime.snapshot_, getInteger("www3_weights.cluster1", 30))
        .WillRepeatedly(Return(10));

    // We return an invalid value here, one that is greater than 100
    // Expect any random value > 10 to always land in cluster2.
    EXPECT_CALL(runtime.snapshot_, getInteger("www3_weights.cluster2", 30))
        .WillRepeatedly(Return(120));
    EXPECT_CALL(runtime.snapshot_, getInteger("www3_weights.cluster3", 40))
        .WillRepeatedly(Return(10));

    EXPECT_EQ("cluster1", config.route(headers, 1005)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 82)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 92)->routeEntry()->clusterName());
  }

  // Weighted Cluster with runtime values, total weight = 10000
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www4.lyft.com", "/foo", "GET");
    EXPECT_CALL(runtime.snapshot_, featureEnabled("www4", 100, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(runtime.snapshot_, getInteger("www4_weights.cluster1", 2000))
        .WillRepeatedly(Return(8000));
    EXPECT_CALL(runtime.snapshot_, getInteger("www4_weights.cluster2", 3000))
        .WillRepeatedly(Return(1000));
    EXPECT_CALL(runtime.snapshot_, getInteger("www4_weights.cluster3", 5000))
        .WillRepeatedly(Return(1000));

    EXPECT_EQ("cluster1", config.route(headers, 1150)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 8100)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 9200)->routeEntry()->clusterName());
  }

  // Weighted Cluster with invalid runtime values, total weight = 10000
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www4.lyft.com", "/foo", "GET");
    EXPECT_CALL(runtime.snapshot_, featureEnabled("www4", 100, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(runtime.snapshot_, getInteger("www4_weights.cluster1", 2000))
        .WillRepeatedly(Return(1000));
    EXPECT_CALL(runtime.snapshot_, getInteger("www4_weights.cluster2", 3000))
        .WillRepeatedly(Return(12000));
    EXPECT_CALL(runtime.snapshot_, getInteger("www4_weights.cluster3", 5000))
        .WillRepeatedly(Return(1000));

    EXPECT_EQ("cluster1", config.route(headers, 500)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 1500)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 9999)->routeEntry()->clusterName());
  }
}

TEST_F(RouteMatcherTest, ExclusiveWeightedClustersOrClusterConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      weighted_clusters:
        clusters:
        - name: cluster1
          weight: 30
        - name: cluster2
          weight: 30
        - name: cluster3
          weight: 40
      cluster: www2
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, WeightedClustersMissingClusterList) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      weighted_clusters:
        runtime_key_prefix: www2
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, WeightedClustersEmptyClustersList) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      weighted_clusters:
        runtime_key_prefix: www2
        clusters: []
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, WeightedClustersSumOFWeightsNotEqualToMax) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            clusters:
              - name: cluster1
                weight: 3
              - name: cluster2
                weight: 3
              - name: cluster3
                weight: 3
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Sum of weights in the weighted_cluster should add up to 100");

  yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            total_weight: 99
            clusters:
              - name: cluster1
                weight: 3
              - name: cluster2
                weight: 3
              - name: cluster3
                weight: 3
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Sum of weights in the weighted_cluster should add up to 99");
}

TEST_F(RouteMatcherTest, TestWeightedClusterWithMissingWeights) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      weighted_clusters:
        clusters:
        - name: cluster1
          weight: 50
        - name: cluster2
          weight: 50
        - name: cluster3
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, TestWeightedClusterInvalidClusterName) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      weighted_clusters:
        clusters:
        - name: cluster1
          weight: 33
        - name: cluster2
          weight: 33
        - name: cluster3-invalid
          weight: 34
  )EOF";

  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("cluster1")))
      .WillRepeatedly(Return(&factory_context_.cluster_manager_.thread_local_cluster_));
  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("cluster2")))
      .WillRepeatedly(Return(&factory_context_.cluster_manager_.thread_local_cluster_));
  EXPECT_CALL(factory_context_.cluster_manager_, get(Eq("cluster3-invalid")))
      .WillRepeatedly(Return(nullptr));

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(RouteMatcherTest, TestWeightedClusterHeaderManipulation) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: www2
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            clusters:
              - name: cluster1
                weight: 50
                request_headers_to_add:
                  - header:
                      key: x-req-cluster
                      value: cluster1
                response_headers_to_add:
                  - header:
                      key: x-resp-cluster
                      value: cluster1
                response_headers_to_remove: [ "x-remove-cluster1" ]
              - name: cluster2
                weight: 50
                request_headers_to_add:
                  - header:
                      key: x-req-cluster
                      value: cluster2
                response_headers_to_add:
                  - header:
                      key: x-resp-cluster
                      value: cluster2
                response_headers_to_remove: [ "x-remove-cluster2" ]
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Http::TestResponseHeaderMapImpl resp_headers({{"x-remove-cluster1", "value"}});
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("cluster1", route->clusterName());

    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("cluster1", headers.get_("x-req-cluster"));

    route->finalizeResponseHeaders(resp_headers, stream_info);
    EXPECT_EQ("cluster1", resp_headers.get_("x-resp-cluster"));
    EXPECT_FALSE(resp_headers.has("x-remove-cluster1"));
  }

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Http::TestResponseHeaderMapImpl resp_headers({{"x-remove-cluster2", "value"}});
    const RouteEntry* route = config.route(headers, 55)->routeEntry();
    EXPECT_EQ("cluster2", route->clusterName());

    route->finalizeRequestHeaders(headers, stream_info, true);
    EXPECT_EQ("cluster2", headers.get_("x-req-cluster"));

    route->finalizeResponseHeaders(resp_headers, stream_info);
    EXPECT_EQ("cluster2", resp_headers.get_("x-resp-cluster"));
    EXPECT_FALSE(resp_headers.has("x-remove-cluster2"));
  }
}

TEST(NullConfigImplTest, All) {
  NullConfigImpl config;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("redirect.lyft.com", "/baz", true, false);
  EXPECT_EQ(nullptr, config.route(headers, stream_info, 0));
  EXPECT_EQ(0UL, config.internalOnlyHeaders().size());
  EXPECT_EQ("", config.name());
}

class BadHttpRouteConfigurationsTest : public testing::Test, public ConfigImplTestBase {};

TEST_F(BadHttpRouteConfigurationsTest, BadRouteConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
fake_entry: fake_type
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(BadHttpRouteConfigurationsTest, BadVirtualHostConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  router:
    cluster: my_cluster
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(BadHttpRouteConfigurationsTest, BadRouteEntryConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
    timeout: 1234s
  )EOF";

  EXPECT_THROW(TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
               EnvoyException);
}

TEST_F(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndPath) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
      path: "/foo"
    route:
      cluster: www2
  )EOF";

#ifndef GTEST_USES_SIMPLE_RE
  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "invalid value oneof field 'path_specifier' is already set. Cannot set '(prefix|path)' for "
      "type oneof");
#else
  EXPECT_THAT_THROWS_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      ::testing::AnyOf(
          ::testing::ContainsRegex(
              "invalid value oneof field 'path_specifier' is already set. Cannot set 'prefix' for "
              "type oneof"),
          ::testing::ContainsRegex(
              "invalid value oneof field 'path_specifier' is already set. Cannot set 'path' for "
              "type oneof")));
#endif
}

TEST_F(BadHttpRouteConfigurationsTest, BadRouteEntryConfigMissingPathSpecifier) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - route:
      cluster: www2
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "RouteValidationError.Match: \\[\"value is required\"\\]");
}

TEST_F(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndRegex) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
      regex: "/[bc]at"
    route:
      cluster: www2
  )EOF";

#ifndef GTEST_USES_SIMPLE_RE
  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "invalid value oneof field 'path_specifier' is already set. Cannot set '(prefix|regex)' for "
      "type oneof");
#else
  EXPECT_THAT_THROWS_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      ::testing::AnyOf(
          ::testing::ContainsRegex(
              "invalid value oneof field 'path_specifier' is already set. Cannot set 'prefix' for "
              "type oneof"),
          ::testing::ContainsRegex(
              "invalid value oneof field 'path_specifier' is already set. Cannot set 'regex' for "
              "type oneof")));
#endif
}

TEST_F(BadHttpRouteConfigurationsTest, BadRouteEntryConfigNoAction) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/api"
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "caused by field: \"action\", reason: is required");
}

TEST_F(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPathAndRegex) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      path: "/foo"
      regex: "/[bc]at"
    route:
      cluster: www2
  )EOF";

#ifndef GTEST_USES_SIMPLE_RE
  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "invalid value oneof field 'path_specifier' is already set. Cannot set '(path|regex)' for "
      "type oneof");
#else
  EXPECT_THAT_THROWS_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      ::testing::AnyOf(
          ::testing::ContainsRegex(
              "invalid value oneof field 'path_specifier' is already set. Cannot set 'path' for "
              "type oneof"),
          ::testing::ContainsRegex(
              "invalid value oneof field 'path_specifier' is already set. Cannot set 'regex' for "
              "type oneof")));
#endif
}

TEST_F(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndPathAndRegex) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
      path: "/foo"
      regex: "/[bc]at"
    route:
      cluster: www2
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "invalid value oneof field 'path_specifier' is already set.");
}

TEST_F(RouteMatcherTest, TestOpaqueConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/api"
    route:
      cluster: ats
    metadata:
      filter_metadata:
        envoy.filters.http.router:
          name1: value1
          name2: value2
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  const std::multimap<std::string, std::string>& opaque_config =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->opaqueConfig();

  EXPECT_EQ(opaque_config.find("name1")->second, "value1");
  EXPECT_EQ(opaque_config.find("name2")->second, "value2");
}

// Test that the deprecated name works for opaque configs.
TEST_F(RouteMatcherTest, DEPRECATED_FEATURE_TEST(TestOpaqueConfigUsingDeprecatedName)) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/api"
    route:
      cluster: ats
    metadata:
      filter_metadata:
        envoy.router:
          name1: value1
          name2: value2
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  const std::multimap<std::string, std::string>& opaque_config =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->opaqueConfig();

  EXPECT_EQ(opaque_config.find("name1")->second, "value1");
  EXPECT_EQ(opaque_config.find("name2")->second, "value2");
}

class RoutePropertyTest : public testing::Test, public ConfigImplTestBase {};

TEST_F(RoutePropertyTest, ExcludeVHRateLimits) {
  std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
  )EOF";

  Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
  std::unique_ptr<TestConfigImpl> config_ptr;

  config_ptr = std::make_unique<TestConfigImpl>(parseRouteConfigurationFromYaml(yaml),
                                                factory_context_, true);
  EXPECT_TRUE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());

  yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
      rate_limits:
      - actions:
        - remote_address: {}
  )EOF";

  config_ptr = std::make_unique<TestConfigImpl>(parseRouteConfigurationFromYaml(yaml),
                                                factory_context_, true);
  EXPECT_FALSE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());

  yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - "*"
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2
      include_vh_rate_limits: true
      rate_limits:
      - actions:
        - remote_address: {}
  )EOF";

  config_ptr = std::make_unique<TestConfigImpl>(parseRouteConfigurationFromYaml(yaml),
                                                factory_context_, true);
  EXPECT_TRUE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());
}

// When allow_origin: and allow_origin_regex: are removed, simply remove them
// and the relevant checks below.
TEST_F(RoutePropertyTest, DEPRECATED_FEATURE_TEST(TestVHostCorsConfig)) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: "default"
    domains: ["*"]
    cors:
      allow_origin: ["test-origin"]
      allow_origin_regex:
      - .*\.envoyproxy\.io
      allow_origin_string_match:
      - safe_regex:
          google_re2: {}
          regex: .*\.envoyproxy\.io
      allow_methods: "test-methods"
      allow_headers: "test-headers"
      expose_headers: "test-expose-headers"
      max_age: "test-max-age"
      allow_credentials: true
      filter_enabled:
        runtime_key: "cors.www.enabled"
        default_value:
          numerator: 0
          denominator: "HUNDRED"
      shadow_enabled:
        runtime_key: "cors.www.shadow_enabled"
        default_value:
          numerator: 100
          denominator: "HUNDRED"
    routes:
      - match:
          prefix: "/api"
        route:
          cluster: "ats"
)EOF";

  Runtime::MockSnapshot snapshot;
  EXPECT_CALL(snapshot, featureEnabled("cors.www.enabled",
                                       Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(false));
  EXPECT_CALL(snapshot, featureEnabled("cors.www.shadow_enabled",
                                       Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(factory_context_.runtime_loader_, snapshot()).WillRepeatedly(ReturnRef(snapshot));

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, false);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)
          ->routeEntry()
          ->virtualHost()
          .corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), false);
  EXPECT_EQ(cors_policy->shadowEnabled(), true);
  EXPECT_EQ(3, cors_policy->allowOrigins().size());
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

TEST_F(RoutePropertyTest, TestRouteCorsConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: "default"
    domains: ["*"]
    routes:
      - match:
          prefix: "/api"
        route:
          cluster: "ats"
          cors:
            allow_origin_string_match:
            - exact: "test-origin"
            allow_methods: "test-methods"
            allow_headers: "test-headers"
            expose_headers: "test-expose-headers"
            max_age: "test-max-age"
            allow_credentials: true
            filter_enabled:
              runtime_key: "cors.www.enabled"
              default_value:
                numerator: 0
                denominator: "HUNDRED"
            shadow_enabled:
              runtime_key: "cors.www.shadow_enabled"
              default_value:
                numerator: 100
                denominator: "HUNDRED"
)EOF";

  Runtime::MockSnapshot snapshot;
  EXPECT_CALL(snapshot, featureEnabled("cors.www.enabled",
                                       Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(false));
  EXPECT_CALL(snapshot, featureEnabled("cors.www.shadow_enabled",
                                       Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(factory_context_.runtime_loader_, snapshot()).WillRepeatedly(ReturnRef(snapshot));

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, false);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), false);
  EXPECT_EQ(cors_policy->shadowEnabled(), true);
  EXPECT_EQ(1, cors_policy->allowOrigins().size());
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

// When allow-origin: is removed, this test can be removed.
TEST_F(RoutePropertyTest, DEPRECATED_FEATURE_TEST(TTestVHostCorsLegacyConfig)) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: default
  domains:
  - "*"
  cors:
    allow_origin:
    - test-origin
    allow_methods: test-methods
    allow_headers: test-headers
    expose_headers: test-expose-headers
    max_age: test-max-age
    allow_credentials: true
  routes:
  - match:
      prefix: "/api"
    route:
      cluster: ats
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)
          ->routeEntry()
          ->virtualHost()
          .corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), true);
  EXPECT_EQ(cors_policy->shadowEnabled(), false);
  EXPECT_EQ(1, cors_policy->allowOrigins().size());
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

// When allow-origin: is removed, this test can be removed.
TEST_F(RoutePropertyTest, DEPRECATED_FEATURE_TEST(TestRouteCorsLegacyConfig)) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/api"
    route:
      cluster: ats
      cors:
        allow_origin:
        - test-origin
        allow_methods: test-methods
        allow_headers: test-headers
        expose_headers: test-expose-headers
        max_age: test-max-age
        allow_credentials: true
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), true);
  EXPECT_EQ(cors_policy->shadowEnabled(), false);
  EXPECT_EQ(1, cors_policy->allowOrigins().size());
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

TEST_F(RoutePropertyTest, TestBadCorsConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/api"
    route:
      cluster: ats
      cors:
        enabled: 0
)EOF";

  EXPECT_THROW_WITH_REGEX(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Unable to parse JSON as proto .*: invalid value 0 for type TYPE_BOOL");
}

TEST_F(RouteMatcherTest, Decorator) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: foo
    decorator:
      operation: myFoo
      propagate: false
  - match:
      prefix: "/bar"
    route:
      cluster: bar
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    Tracing::MockSpan span;
    EXPECT_CALL(span, setOperation(Eq("myFoo")));
    route->decorator()->apply(span);
    EXPECT_EQ(false, route->decorator()->propagate());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ(nullptr, route->decorator());
  }
}

class CustomRequestHeadersTest : public testing::Test, public ConfigImplTestBase {};

TEST_F(CustomRequestHeadersTest, AddNewHeader) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - lyft.com
  - www.lyft.com
  - w.lyft.com
  - ww.lyft.com
  - wwww.lyft.com
  request_headers_to_add:
  - header:
      key: x-client-ip
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  routes:
  - match:
      prefix: "/new_endpoint"
    route:
      prefix_rewrite: "/api/new_endpoint"
      cluster: www2
    request_headers_to_add:
    - header:
        key: x-client-ip
        value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
request_headers_to_add:
- header:
    key: x-client-ip
    value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  )EOF";
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
  const RouteEntry* route = config.route(headers, 0)->routeEntry();
  route->finalizeRequestHeaders(headers, stream_info, true);
  EXPECT_EQ("127.0.0.1", headers.get_("x-client-ip"));
}

TEST_F(CustomRequestHeadersTest, CustomHeaderWrongFormat) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - lyft.com
  - www.lyft.com
  - w.lyft.com
  - ww.lyft.com
  - wwww.lyft.com
  request_headers_to_add:
  - header:
      key: x-client-ip
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  routes:
  - match:
      prefix: "/new_endpoint"
    route:
      prefix_rewrite: "/api/new_endpoint"
      cluster: www2
    request_headers_to_add:
    - header:
        key: x-client-ip
        value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT"
request_headers_to_add:
- header:
    key: x-client-ip
    value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT"
  )EOF";
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
      EnvoyException,
      "Invalid header configuration. Un-terminated variable expression "
      "'DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT'");
}

TEST(MetadataMatchCriteriaImpl, Create) {
  auto v1 = ProtobufWkt::Value();
  v1.set_string_value("v1");
  auto v2 = ProtobufWkt::Value();
  v2.set_number_value(2.0);
  auto v3 = ProtobufWkt::Value();
  v3.set_bool_value(true);

  auto metadata_struct = ProtobufWkt::Struct();
  auto mutable_fields = metadata_struct.mutable_fields();
  mutable_fields->insert({"a", v1});
  mutable_fields->insert({"b", v2});
  mutable_fields->insert({"c", v3});

  auto matches = MetadataMatchCriteriaImpl(metadata_struct);

  EXPECT_EQ(matches.metadataMatchCriteria().size(), 3);
  auto it = matches.metadataMatchCriteria().begin();
  EXPECT_EQ((*it)->name(), "a");
  EXPECT_EQ((*it)->value().value().string_value(), "v1");
  it++;

  EXPECT_EQ((*it)->name(), "b");
  EXPECT_EQ((*it)->value().value().number_value(), 2.0);
  it++;

  EXPECT_EQ((*it)->name(), "c");
  EXPECT_EQ((*it)->value().value().bool_value(), true);
}

TEST(MetadataMatchCriteriaImpl, Merge) {
  auto pv1 = ProtobufWkt::Value();
  pv1.set_string_value("v1");
  auto pv2 = ProtobufWkt::Value();
  pv2.set_number_value(2.0);
  auto pv3 = ProtobufWkt::Value();
  pv3.set_bool_value(true);

  auto parent_struct = ProtobufWkt::Struct();
  auto parent_fields = parent_struct.mutable_fields();
  parent_fields->insert({"a", pv1});
  parent_fields->insert({"b", pv2});
  parent_fields->insert({"c", pv3});

  auto parent_matches = MetadataMatchCriteriaImpl(parent_struct);

  auto v1 = ProtobufWkt::Value();
  v1.set_string_value("override1");
  auto v2 = ProtobufWkt::Value();
  v2.set_string_value("v2");
  auto v3 = ProtobufWkt::Value();
  v3.set_string_value("override3");

  auto metadata_struct = ProtobufWkt::Struct();
  auto mutable_fields = metadata_struct.mutable_fields();
  mutable_fields->insert({"a", v1});
  mutable_fields->insert({"b++", v2});
  mutable_fields->insert({"c", v3});

  MetadataMatchCriteriaConstPtr matches = parent_matches.mergeMatchCriteria(metadata_struct);

  EXPECT_EQ(matches->metadataMatchCriteria().size(), 4);
  auto it = matches->metadataMatchCriteria().begin();
  EXPECT_EQ((*it)->name(), "a");
  EXPECT_EQ((*it)->value().value().string_value(), "override1");
  it++;

  EXPECT_EQ((*it)->name(), "b");
  EXPECT_EQ((*it)->value().value().number_value(), 2.0);
  it++;

  EXPECT_EQ((*it)->name(), "b++");
  EXPECT_EQ((*it)->value().value().string_value(), "v2");
  it++;

  EXPECT_EQ((*it)->name(), "c");
  EXPECT_EQ((*it)->value().value().string_value(), "override3");
}

TEST(MetadataMatchCriteriaImpl, Filter) {
  auto pv1 = ProtobufWkt::Value();
  pv1.set_string_value("v1");
  auto pv2 = ProtobufWkt::Value();
  pv2.set_number_value(2.0);
  auto pv3 = ProtobufWkt::Value();
  pv3.set_bool_value(true);

  auto metadata_matches = ProtobufWkt::Struct();
  auto parent_fields = metadata_matches.mutable_fields();
  parent_fields->insert({"a", pv1});
  parent_fields->insert({"b", pv2});
  parent_fields->insert({"c", pv3});

  auto matches = MetadataMatchCriteriaImpl(metadata_matches);
  auto filtered_matches1 = matches.filterMatchCriteria({"b", "c"});
  auto filtered_matches2 = matches.filterMatchCriteria({"a"});

  EXPECT_EQ(matches.metadataMatchCriteria().size(), 3);
  EXPECT_EQ(filtered_matches1->metadataMatchCriteria().size(), 2);
  EXPECT_EQ(filtered_matches2->metadataMatchCriteria().size(), 1);

  EXPECT_EQ(filtered_matches1->metadataMatchCriteria()[0]->name(), "b");
  EXPECT_EQ(filtered_matches1->metadataMatchCriteria()[0]->value().value().number_value(), 2.0);
  EXPECT_EQ(filtered_matches1->metadataMatchCriteria()[1]->name(), "c");
  EXPECT_EQ(filtered_matches1->metadataMatchCriteria()[1]->value().value().bool_value(), true);

  EXPECT_EQ(filtered_matches2->metadataMatchCriteria()[0]->name(), "a");
  EXPECT_EQ(filtered_matches2->metadataMatchCriteria()[0]->value().value().string_value(), "v1");
}

class RouteEntryMetadataMatchTest : public testing::Test, public ConfigImplTestBase {};

TEST_F(RouteEntryMetadataMatchTest, ParsesMetadata) {
  auto route_config = envoy::config::route::v3::RouteConfiguration();
  auto* vhost = route_config.add_virtual_hosts();
  vhost->set_name("vhost");
  vhost->add_domains("www.lyft.com");

  // route provides metadata matches combined from RouteAction and WeightedCluster
  auto* route = vhost->add_routes();
  route->mutable_match()->set_prefix("/both");
  auto* route_action = route->mutable_route();
  auto* weighted_cluster = route_action->mutable_weighted_clusters()->add_clusters();
  weighted_cluster->set_name("www1");
  weighted_cluster->mutable_weight()->set_value(100);
  Envoy::Config::Metadata::mutableMetadataValue(*weighted_cluster->mutable_metadata_match(),
                                                Envoy::Config::MetadataFilters::get().ENVOY_LB,
                                                "r1_wc_key")
      .set_string_value("r1_wc_value");
  Envoy::Config::Metadata::mutableMetadataValue(*route_action->mutable_metadata_match(),
                                                Envoy::Config::MetadataFilters::get().ENVOY_LB,
                                                "r1_key")
      .set_string_value("r1_value");

  // route provides metadata matches from WeightedCluster only
  route = vhost->add_routes();
  route->mutable_match()->set_prefix("/cluster-only");
  route_action = route->mutable_route();
  weighted_cluster = route_action->mutable_weighted_clusters()->add_clusters();
  weighted_cluster->set_name("www2");
  weighted_cluster->mutable_weight()->set_value(100);
  Envoy::Config::Metadata::mutableMetadataValue(*weighted_cluster->mutable_metadata_match(),
                                                Envoy::Config::MetadataFilters::get().ENVOY_LB,
                                                "r2_wc_key")
      .set_string_value("r2_wc_value");

  // route provides metadata matches from RouteAction only
  route = vhost->add_routes();
  route->mutable_match()->set_prefix("/route-only");
  route_action = route->mutable_route();
  route_action->set_cluster("www3");
  Envoy::Config::Metadata::mutableMetadataValue(*route_action->mutable_metadata_match(),
                                                Envoy::Config::MetadataFilters::get().ENVOY_LB,
                                                "r3_key")
      .set_string_value("r3_value");

  // route provides metadata matches from RouteAction (but WeightedCluster exists)
  route = vhost->add_routes();
  route->mutable_match()->set_prefix("/cluster-passthrough");
  route_action = route->mutable_route();
  weighted_cluster = route_action->mutable_weighted_clusters()->add_clusters();
  weighted_cluster->set_name("www4");
  weighted_cluster->mutable_weight()->set_value(100);
  Envoy::Config::Metadata::mutableMetadataValue(*route_action->mutable_metadata_match(),
                                                Envoy::Config::MetadataFilters::get().ENVOY_LB,
                                                "r4_key")
      .set_string_value("r4_value");

  TestConfigImpl config(route_config, factory_context_, true);

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("www.lyft.com", "/both", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());

    auto* route_entry = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www1", route_entry->clusterName());
    auto* matches = route_entry->metadataMatchCriteria();
    EXPECT_NE(matches, nullptr);
    EXPECT_EQ(matches->metadataMatchCriteria().size(), 2);
    EXPECT_EQ(matches->metadataMatchCriteria().at(0)->name(), "r1_key");
    EXPECT_EQ(matches->metadataMatchCriteria().at(1)->name(), "r1_wc_key");
  }

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("www.lyft.com", "/cluster-only", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());

    auto* route_entry = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route_entry->clusterName());
    auto* matches = route_entry->metadataMatchCriteria();
    EXPECT_NE(matches, nullptr);
    EXPECT_EQ(matches->metadataMatchCriteria().size(), 1);
    EXPECT_EQ(matches->metadataMatchCriteria().at(0)->name(), "r2_wc_key");
  }

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("www.lyft.com", "/route-only", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());

    auto* route_entry = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www3", route_entry->clusterName());
    auto* matches = route_entry->metadataMatchCriteria();
    EXPECT_NE(matches, nullptr);
    EXPECT_EQ(matches->metadataMatchCriteria().size(), 1);
    EXPECT_EQ(matches->metadataMatchCriteria().at(0)->name(), "r3_key");
  }

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("www.lyft.com", "/cluster-passthrough", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());

    auto* route_entry = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www4", route_entry->clusterName());
    auto* matches = route_entry->metadataMatchCriteria();
    EXPECT_NE(matches, nullptr);
    EXPECT_EQ(matches->metadataMatchCriteria().size(), 1);
    EXPECT_EQ(matches->metadataMatchCriteria().at(0)->name(), "r4_key");
  }
}

class ConfigUtilityTest : public testing::Test, public ConfigImplTestBase {};

TEST_F(ConfigUtilityTest, ParseResponseCode) {
  const std::vector<
      std::pair<envoy::config::route::v3::RedirectAction::RedirectResponseCode, Http::Code>>
      test_set = {
          std::make_pair(envoy::config::route::v3::RedirectAction::MOVED_PERMANENTLY,
                         Http::Code::MovedPermanently),
          std::make_pair(envoy::config::route::v3::RedirectAction::FOUND, Http::Code::Found),
          std::make_pair(envoy::config::route::v3::RedirectAction::SEE_OTHER, Http::Code::SeeOther),
          std::make_pair(envoy::config::route::v3::RedirectAction::TEMPORARY_REDIRECT,
                         Http::Code::TemporaryRedirect),
          std::make_pair(envoy::config::route::v3::RedirectAction::PERMANENT_REDIRECT,
                         Http::Code::PermanentRedirect)};
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, ConfigUtility::parseRedirectResponseCode(test_case.first));
  }
}

TEST_F(ConfigUtilityTest, ParseDirectResponseBody) {
  envoy::config::route::v3::Route route;
  EXPECT_EQ(EMPTY_STRING, ConfigUtility::parseDirectResponseBody(route, *api_));

  route.mutable_direct_response()->mutable_body()->set_filename("missing_file");
  EXPECT_THROW_WITH_MESSAGE(ConfigUtility::parseDirectResponseBody(route, *api_), EnvoyException,
                            "response body file missing_file does not exist");

  std::string body(4097, '*');
  auto filename = TestEnvironment::writeStringToFileForTest("body", body);
  route.mutable_direct_response()->mutable_body()->set_filename(filename);
  std::string expected_message("response body file " + filename +
                               " size is 4097 bytes; maximum is 4096");
  EXPECT_THROW_WITH_MESSAGE(ConfigUtility::parseDirectResponseBody(route, *api_), EnvoyException,
                            expected_message);
}

TEST_F(RouteConfigurationV2, RedirectCode) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: redirect
    domains: [redirect.lyft.com]
    routes:
      - match: { prefix: "/"}
        redirect: { host_redirect: new.lyft.com, response_code: TEMPORARY_REDIRECT }

  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(Http::Code::TemporaryRedirect,
              config.route(headers, 0)->directResponseEntry()->responseCode());
  }
}

// Test the parsing of direct response configurations within routes.
TEST_F(RouteConfigurationV2, DirectResponse) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: direct
    domains: [example.com]
    routes:
      - match: { prefix: "/"}
        direct_response: { status: 200, body: { inline_string: "content" } }
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  const auto* direct_response =
      config.route(genHeaders("example.com", "/", "GET"), 0)->directResponseEntry();
  EXPECT_NE(nullptr, direct_response);
  EXPECT_EQ(Http::Code::OK, direct_response->responseCode());
  EXPECT_STREQ("content", direct_response->responseBody().c_str());
}

// Test the parsing of a direct response configuration where the response body is too large.
TEST_F(RouteConfigurationV2, DirectResponseTooLarge) {
  std::string response_body(4097, 'A');
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: direct
    domains: [example.com]
    routes:
      - match: { prefix: "/"}
        direct_response:
          status: 200
          body:
            inline_string: )EOF" +
                           response_body + "\n";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl invalid_config(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
      EnvoyException, "response body size is 4097 bytes; maximum is 4096");
}

void checkPathMatchCriterion(const Route* route, const std::string& expected_matcher,
                             PathMatchType expected_type) {
  ASSERT_NE(nullptr, route);
  const auto route_entry = route->routeEntry();
  ASSERT_NE(nullptr, route_entry);
  const auto& match_criterion = route_entry->pathMatchCriterion();
  EXPECT_EQ(expected_matcher, match_criterion.matcher());
  EXPECT_EQ(expected_type, match_criterion.matchType());
}

// Test loading broken config throws EnvoyException.
TEST_F(RouteConfigurationV2, BrokenTypedMetadata) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/"}
        route: { cluster: www2 }
        metadata: { filter_metadata: { com.bar.foo: { baz: test_value },
                                       baz: {} } }
  )EOF";
  BazFactory baz_factory;
  Registry::InjectFactory<HttpRouteTypedMetadataFactory> registered_factory(baz_factory);
  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
      Envoy::EnvoyException, "Cannot create a Baz when metadata is empty.");
}

TEST_F(RouteConfigurationV2, RouteConfigGetters) {
  const std::string yaml = R"EOF(
name: foo
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/rege[xy]"
        route: { cluster: ww2 }
      - match: { path: "/exact-path" }
        route: { cluster: ww2 }
      - match: { prefix: "/"}
        route: { cluster: www2 }
        metadata: { filter_metadata: { com.bar.foo: { baz: test_value }, baz: {name: bluh} } }
  )EOF";
  BazFactory baz_factory;
  Registry::InjectFactory<HttpRouteTypedMetadataFactory> registered_factory(baz_factory);
  const TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  checkPathMatchCriterion(config.route(genHeaders("www.foo.com", "/regex", "GET"), 0).get(),
                          "/rege[xy]", PathMatchType::Regex);
  checkPathMatchCriterion(config.route(genHeaders("www.foo.com", "/exact-path", "GET"), 0).get(),
                          "/exact-path", PathMatchType::Exact);
  const auto route = config.route(genHeaders("www.foo.com", "/", "GET"), 0);
  checkPathMatchCriterion(route.get(), "/", PathMatchType::Prefix);

  const auto route_entry = route->routeEntry();
  const auto& metadata = route_entry->metadata();
  const auto& typed_metadata = route_entry->typedMetadata();

  EXPECT_EQ("test_value",
            Envoy::Config::Metadata::metadataValue(&metadata, "com.bar.foo", "baz").string_value());
  EXPECT_NE(nullptr, typed_metadata.get<Baz>(baz_factory.name()));
  EXPECT_EQ("bluh", typed_metadata.get<Baz>(baz_factory.name())->name);

  EXPECT_EQ("bar", symbol_table_->toString(route_entry->virtualHost().statName()));
  EXPECT_EQ("foo", route_entry->virtualHost().routeConfig().name());
}

TEST_F(RouteConfigurationV2, RouteTracingConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/first"
        tracing:
          client_sampling:
            numerator: 1
        route: { cluster: ww2 }
      - match:
          safe_regex:
            google_re2: {}
            regex: "/second"
        tracing:
          overall_sampling:
            numerator: 1
        route: { cluster: ww2 }
      - match: { path: "/third" }
        tracing:
          client_sampling:
            numerator: 1
          random_sampling:
            numerator: 200
            denominator: 1
          overall_sampling:
            numerator: 3
          custom_tags:
          - tag: ltag
            literal:
              value: lvalue
          - tag: etag
            environment:
              name: E_TAG
          - tag: rtag
            request_header:
              name: X-Tag
          - tag: mtag
            metadata:
              kind: { route: {} }
              metadata_key:
                key: com.bar.foo
                path: [ { key: xx }, { key: yy } ]
        route: { cluster: ww2 }
  )EOF";
  BazFactory baz_factory;
  Registry::InjectFactory<HttpRouteTypedMetadataFactory> registered_factory(baz_factory);
  const TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  const auto route1 = config.route(genHeaders("www.foo.com", "/first", "GET"), 0);
  const auto route2 = config.route(genHeaders("www.foo.com", "/second", "GET"), 0);
  const auto route3 = config.route(genHeaders("www.foo.com", "/third", "GET"), 0);

  // Check default values for random and overall sampling
  EXPECT_EQ(100, route1->tracingConfig()->getRandomSampling().numerator());
  EXPECT_EQ(0, route1->tracingConfig()->getRandomSampling().denominator());
  EXPECT_EQ(100, route1->tracingConfig()->getOverallSampling().numerator());
  EXPECT_EQ(0, route1->tracingConfig()->getOverallSampling().denominator());

  // Check default values for client sampling
  EXPECT_EQ(100, route2->tracingConfig()->getClientSampling().numerator());
  EXPECT_EQ(0, route2->tracingConfig()->getClientSampling().denominator());

  EXPECT_EQ(1, route3->tracingConfig()->getClientSampling().numerator());
  EXPECT_EQ(0, route3->tracingConfig()->getClientSampling().denominator());
  EXPECT_EQ(200, route3->tracingConfig()->getRandomSampling().numerator());
  EXPECT_EQ(1, route3->tracingConfig()->getRandomSampling().denominator());
  EXPECT_EQ(3, route3->tracingConfig()->getOverallSampling().numerator());
  EXPECT_EQ(0, route3->tracingConfig()->getOverallSampling().denominator());

  std::vector<std::string> custom_tags{"ltag", "etag", "rtag", "mtag"};
  const Tracing::CustomTagMap& map = route3->tracingConfig()->getCustomTags();
  for (const std::string& custom_tag : custom_tags) {
    EXPECT_NE(map.find(custom_tag), map.end());
  }
}

// Test to check Prefix Rewrite for redirects
TEST_F(RouteConfigurationV2, RedirectPrefixRewrite) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: redirect
    domains: [redirect.lyft.com]
    routes:
      - match: { prefix: "/prefix"}
        redirect: { prefix_rewrite: "/new/prefix" }
      - match: { path: "/path/" }
        redirect: { prefix_rewrite: "/new/path/" }
      - match: { prefix: "/host/prefix" }
        redirect: { host_redirect: new.lyft.com, prefix_rewrite: "/new/prefix"}
      - match:
          safe_regex:
            google_re2: {}
            regex: "/[r][e][g][e][x].*"
        redirect: { prefix_rewrite: "/new/regex-prefix/" }
      - match: { prefix: "/http/prefix"}
        redirect: { prefix_rewrite: "/https/prefix" , https_redirect: true }
      - match: { prefix: "/ignore-this/"}
        redirect: { prefix_rewrite: "/" }
      - match: { prefix: "/ignore-this"}
        redirect: { prefix_rewrite: "/" }
      - match: { prefix: "/ignore-substring"}
        redirect: { prefix_rewrite: "/" }
      - match: { prefix: "/service-hello/"}
        redirect: { prefix_rewrite: "/" }
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/prefix/some/path/?lang=eng&con=US", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/new/prefix/some/path/?lang=eng&con=US",
              redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/", true, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("https://redirect.lyft.com/new/path/", redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host/prefix/1", true, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("https://new.lyft.com/new/prefix/1", redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/regex/hello/", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/new/regex-prefix/", redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/http/prefix/", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("https://redirect.lyft.com/https/prefix/", redirect->newPath(headers));
  }
  {
    // The following matches to the redirect action match value equals to `/ignore-this` instead of
    // `/ignore-this/`.
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/ignore-this", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/", redirect->newPath(headers));
  }
  {
    // The following matches to the redirect action match value equals to `/ignore-this/` instead of
    // `/ignore-this`.
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/ignore-this/", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/", redirect->newPath(headers));
  }
  {
    // The same as previous test request, the following matches to the redirect action match value
    // equals to `/ignore-this/` instead of `/ignore-this`.
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "redirect.lyft.com", "/ignore-this/however/use/the/rest/of/this/path", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/however/use/the/rest/of/this/path",
              redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/ignore-this/use/", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/use/", redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/ignore-substringto/use/", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/to/use/", redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/ignore-substring-to/use/", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/-to/use/", redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/service-hello/a/b/c", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/a/b/c", redirect->newPath(headers));
  }
}

TEST_F(RouteConfigurationV2, PathRedirectQueryNotPreserved) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_query_string_in_path_redirects", "false"}});

  std::string yaml = R"EOF(
virtual_hosts:
  - name: redirect
    domains: [redirect.lyft.com]
    routes:
      - match: { path: "/path/redirect/"}
        redirect: { path_redirect: "/new/path-redirect/" }
      - match: { path: "/path/redirect/strip-query/true"}
        redirect: { path_redirect: "/new/path-redirect/", strip_query: "true" }
      - match: { path: "/path/redirect/query"}
        redirect: { path_redirect: "/new/path-redirect?foo=1" }
      - match: { path: "/path/redirect/query-with-strip"}
        redirect: { path_redirect: "/new/path-redirect?foo=2", strip_query: "true" }
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/?lang=eng&con=US", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect/",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "redirect.lyft.com", "/path/redirect/strip-query/true?lang=eng&con=US", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect/",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/query", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=1",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/query?bar=1", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=1",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/query-with-strip", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=2",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "redirect.lyft.com", "/path/redirect/query-with-strip?bar=1", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=2",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
}

// Test to check Strip Query for redirect messages
TEST_F(RouteConfigurationV2, RedirectStripQuery) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: redirect
    domains: [redirect.lyft.com]
    routes:
      - match: { prefix: "/query/true"}
        redirect: { prefix_rewrite: "/new/prefix", strip_query: "true" }
      - match: { prefix: "/query/false" }
        redirect: { prefix_rewrite: "/new/prefix", strip_query: "false" }
      - match: { path: "/host/query-default" }
        redirect: { host_redirect: new.lyft.com }
      - match: { path: "/path/redirect/"}
        redirect: { path_redirect: "/new/path-redirect/" }
      - match: { path: "/path/redirect/strip-query/true"}
        redirect: { path_redirect: "/new/path-redirect/", strip_query: "true" }
      - match: { path: "/path/redirect/query"}
        redirect: { path_redirect: "/new/path-redirect?foo=1" }
      - match: { path: "/path/redirect/query-with-strip"}
        redirect: { path_redirect: "/new/path-redirect?foo=2", strip_query: "true" }
      - match: { prefix: "/all/combinations"}
        redirect: { host_redirect: "new.lyft.com", prefix_rewrite: "/new/prefix" , https_redirect: "true", strip_query: "true" }
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));

  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/query/true?lang=eng&con=US", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("http://redirect.lyft.com/new/prefix", redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "redirect.lyft.com", "/query/false/some/path?lang=eng&con=US", true, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("https://redirect.lyft.com/new/prefix/some/path?lang=eng&con=US",
              redirect->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/host/query-default?lang=eng&con=US", true, false);
    EXPECT_EQ("https://new.lyft.com/host/query-default?lang=eng&con=US",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/?lang=eng&con=US", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect/?lang=eng&con=US",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "redirect.lyft.com", "/path/redirect/strip-query/true?lang=eng&con=US", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect/",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/query", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=1",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/query?bar=1", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=1",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("redirect.lyft.com", "/path/redirect/query-with-strip", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=2",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "redirect.lyft.com", "/path/redirect/query-with-strip?bar=1", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new/path-redirect?foo=2",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
  }
  {
    Http::TestRequestHeaderMapImpl headers = genRedirectHeaders(
        "redirect.lyft.com", "/all/combinations/here/we/go?key=value", false, false);
    const DirectResponseEntry* redirect = config.route(headers, 0)->directResponseEntry();
    redirect->rewritePathHeader(headers, true);
    EXPECT_EQ("https://new.lyft.com/new/prefix/here/we/go", redirect->newPath(headers));
  }
}

TEST_F(RouteMatcherTest, HeaderMatchedRoutingV2) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          headers:
            - name: test_header
              exact_match: test
        route:
          cluster: local_service_with_headers
      - match:
          prefix: "/"
          headers:
            - name: test_header_multiple1
              exact_match: test1
            - name: test_header_multiple2
              exact_match: test2
        route:
          cluster: local_service_with_multiple_headers
      - match:
          prefix: "/"
          headers:
            - name: test_header_presence
        route:
          cluster: local_service_with_empty_headers
      - match:
          prefix: "/"
          headers:
            - name: test_header_pattern
              safe_regex_match:
                google_re2: {}
                regex: "^user=test-\\d+$"
        route:
          cluster: local_service_with_header_pattern_set_regex
      - match:
          prefix: "/"
          headers:
            - name: test_header_pattern
              exact_match: "^customer=test-\\d+$"
        route:
          cluster: local_service_with_header_pattern_unset_regex
      - match:
          prefix: "/"
          headers:
            - name: test_header_range
              range_match:
                 start: -9223372036854775808
                 end: -10
        route:
          cluster: local_service_with_header_range_test1
      - match:
          prefix: "/"
          headers:
            - name: test_header_multiple_range
              range_match:
                 start: -10
                 end: 1
            - name: test_header_multiple_exact
              exact_match: test
        route:
          cluster: local_service_with_header_range_test2
      - match:
          prefix: "/"
          headers:
            - name: test_header_range
              range_match:
                 start: 1
                 end: 10
        route:
          cluster: local_service_with_header_range_test3
      - match:
          prefix: "/"
          headers:
            - name: test_header_range
              range_match:
                 start: 9223372036854775801
                 end: 9223372036854775807
        route:
          cluster: local_service_with_header_range_test4
      - match:
          prefix: "/"
          headers:
            - name: test_header_range
              exact_match: "9223372036854775807"
        route:
          cluster: local_service_with_header_range_test5
      - match:
          prefix: "/"
        route:
          cluster: local_service_without_headers
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    EXPECT_EQ("local_service_without_headers",
              config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header", "test");
    EXPECT_EQ("local_service_with_headers", config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_multiple1", "test1");
    headers.addCopy("test_header_multiple2", "test2");
    EXPECT_EQ("local_service_with_multiple_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("non_existent_header", "foo");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_presence", "test");
    EXPECT_EQ("local_service_with_empty_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "user=test-1223");
    EXPECT_EQ("local_service_with_header_pattern_set_regex",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "customer=test-1223");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_range", "-9223372036854775808");
    EXPECT_EQ("local_service_with_header_range_test1",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_multiple_range", "-9");
    headers.addCopy("test_header_multiple_exact", "test");
    EXPECT_EQ("local_service_with_header_range_test2",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_range", "9");
    EXPECT_EQ("local_service_with_header_range_test3",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_range", "9223372036854775807");
    EXPECT_EQ("local_service_with_header_range_test5",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_multiple_range", "-9");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_range", "19");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
}

// Test Route Matching based on connection Tls Context.
// Validate configured and default settings are routed to the correct cluster.
TEST_F(RouteMatcherTest, TlsContextMatching) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/peer-cert-test"
          tls_context:
            presented: true
        route:
          cluster: server_peer-cert-presented
      - match:
          prefix: "/peer-cert-test"
          tls_context:
            presented: false
        route:
          cluster: server_peer-cert-not-presented
      - match:
          prefix: "/peer-validated-cert-test"
          tls_context:
            validated: true
        route:
          cluster: server_peer-cert-validated
      - match:
          prefix: "/peer-validated-cert-test"
          tls_context:
            validated: false
        route:
          cluster: server_peer-cert-not-validated
      - match:
          prefix: "/peer-cert-no-tls-context-match"
        route:
          cluster: server_peer-cert-no-tls-context-match
      - match:
          prefix: "/"
        route:
          cluster: local_service_without_headers
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(true));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(true));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/peer-cert-test", "GET");
    EXPECT_EQ("server_peer-cert-presented",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(false));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(true));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers = genHeaders("www.lyft.com", "/peer-cert-test", "GET");
    EXPECT_EQ("server_peer-cert-not-presented",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(false));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(true));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/peer-cert-no-tls-context-match", "GET");
    EXPECT_EQ("server_peer-cert-no-tls-context-match",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(true));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(true));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/peer-cert-no-tls-context-match", "GET");
    EXPECT_EQ("server_peer-cert-no-tls-context-match",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(true));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(true));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/peer-validated-cert-test", "GET");
    EXPECT_EQ("server_peer-cert-validated",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(true));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(false));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/peer-validated-cert-test", "GET");
    EXPECT_EQ("server_peer-cert-not-validated",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(true));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(false));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/peer-cert-no-tls-context-match", "GET");
    EXPECT_EQ("server_peer-cert-no-tls-context-match",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, peerCertificatePresented()).WillRepeatedly(Return(true));
    EXPECT_CALL(*connection_info, peerCertificateValidated()).WillRepeatedly(Return(true));
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/peer-cert-no-tls-context-match", "GET");
    EXPECT_EQ("server_peer-cert-no-tls-context-match",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }

  {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    std::shared_ptr<Ssl::MockConnectionInfo> connection_info;
    EXPECT_CALL(stream_info, downstreamSslConnection()).WillRepeatedly(Return(connection_info));

    Http::TestRequestHeaderMapImpl headers =
        genHeaders("www.lyft.com", "/peer-cert-no-tls-context-match", "GET");
    EXPECT_EQ("server_peer-cert-no-tls-context-match",
              config.route(headers, stream_info, 0)->routeEntry()->clusterName());
  }
}

TEST_F(RouteConfigurationV2, RegexPrefixWithNoRewriteWorksWhenPathChanged) {

  // Setup regex route entry. the regex is trivial, that's ok as we only want to test that
  // path change works.
  std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [regex.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route: { cluster: some-cluster }
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

  {
    // Get our regex route entry
    Http::TestRequestHeaderMapImpl headers =
        genRedirectHeaders("regex.lyft.com", "/regex", true, false);
    const RouteEntry* route_entry = config.route(headers, 0)->routeEntry();

    // simulate a filter changing the path
    headers.remove(":path");
    headers.addCopy(":path", "/not-the-original-regex");

    // no re-write was specified; so this should not throw
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    EXPECT_NO_THROW(route_entry->finalizeRequestHeaders(headers, stream_info, false));
  }
}

TEST_F(RouteConfigurationV2, NoIdleTimeout) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const RouteEntry* route_entry = config.route(headers, 0)->routeEntry();
  EXPECT_EQ(absl::nullopt, route_entry->idleTimeout());
}

TEST_F(RouteConfigurationV2, ZeroIdleTimeout) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          idle_timeout: 0s
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const RouteEntry* route_entry = config.route(headers, 0)->routeEntry();
  EXPECT_EQ(0, route_entry->idleTimeout().value().count());
}

TEST_F(RouteConfigurationV2, ExplicitIdleTimeout) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          idle_timeout: 7s
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const RouteEntry* route_entry = config.route(headers, 0)->routeEntry();
  EXPECT_EQ(7 * 1000, route_entry->idleTimeout().value().count());
}

TEST_F(RouteConfigurationV2, RetriableStatusCodes) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          retry_policy:
            retriable_status_codes: [100, 200]
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const auto& retry_policy = config.route(headers, 0)->routeEntry()->retryPolicy();
  const std::vector<uint32_t> expected_codes{100, 200};
  EXPECT_EQ(expected_codes, retry_policy.retriableStatusCodes());
}

TEST_F(RouteConfigurationV2, RetriableHeaders) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          retry_policy:
            retriable_headers:
            - name: ":status"
              exact_match: "500"
            - name: X-Upstream-Pushback
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const auto& retry_policy = config.route(headers, 0)->routeEntry()->retryPolicy();
  ASSERT_EQ(2, retry_policy.retriableHeaders().size());

  Http::TestResponseHeaderMapImpl expected_0{{":status", "500"}};
  Http::TestResponseHeaderMapImpl unexpected_0{{":status", "200"}};
  Http::TestResponseHeaderMapImpl expected_1{{"x-upstream-pushback", "bar"}};
  Http::TestResponseHeaderMapImpl unexpected_1{{"x-test", "foo"}};

  EXPECT_TRUE(retry_policy.retriableHeaders()[0]->matchesHeaders(expected_0));
  EXPECT_FALSE(retry_policy.retriableHeaders()[0]->matchesHeaders(unexpected_0));
  EXPECT_TRUE(retry_policy.retriableHeaders()[1]->matchesHeaders(expected_1));
  EXPECT_FALSE(retry_policy.retriableHeaders()[1]->matchesHeaders(unexpected_1));
}

TEST_F(RouteConfigurationV2, UpgradeConfigs) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          upgrade_configs:
            - upgrade_type: Websocket
            - upgrade_type: disabled
              enabled: false
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const RouteEntry::UpgradeMap& upgrade_map = config.route(headers, 0)->routeEntry()->upgradeMap();
  EXPECT_TRUE(upgrade_map.find("websocket")->second);
  EXPECT_TRUE(upgrade_map.find("foo") == upgrade_map.end());
  EXPECT_FALSE(upgrade_map.find("disabled")->second);
}

TEST_F(RouteConfigurationV2, DuplicateUpgradeConfigs) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          upgrade_configs:
            - upgrade_type: Websocket
            - upgrade_type: WebSocket
              enabled: false
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Duplicate upgrade WebSocket");
}

TEST_F(RouteConfigurationV2, BadConnectConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          upgrade_configs:
            - upgrade_type: Websocket
              connect_config: {}
              enabled: false
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Non-CONNECT upgrade type Websocket has ConnectConfig");
}

// Verifies that we're creating a new instance of the retry plugins on each call instead of always
// returning the same one.
TEST_F(RouteConfigurationV2, RetryPluginsAreNotReused) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          retry_policy:
            retry_host_predicate:
            - name: envoy.test_host_predicate
            retry_priority:
              name: envoy.test_retry_priority
  )EOF";

  Upstream::MockRetryPriority priority{{}, {}};
  Upstream::MockRetryPriorityFactory priority_factory(priority);
  Registry::InjectFactory<Upstream::RetryPriorityFactory> inject_priority_factory(priority_factory);

  Upstream::TestRetryHostPredicateFactory host_predicate_factory;
  Registry::InjectFactory<Upstream::RetryHostPredicateFactory> inject_predicate_factory(
      host_predicate_factory);

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const auto& retry_policy = config.route(headers, 0)->routeEntry()->retryPolicy();
  const auto priority1 = retry_policy.retryPriority();
  const auto priority2 = retry_policy.retryPriority();
  EXPECT_NE(priority1, priority2);
  const auto predicates1 = retry_policy.retryHostPredicates();
  const auto predicates2 = retry_policy.retryHostPredicates();
  EXPECT_NE(predicates1, predicates2);
}

TEST_F(RouteConfigurationV2, InternalRedirectIsDisabledWhenNotSpecifiedInRouteAction) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const auto& internal_redirect_policy =
      config.route(headers, 0)->routeEntry()->internalRedirectPolicy();
  EXPECT_FALSE(internal_redirect_policy.enabled());
}

TEST_F(RouteConfigurationV2, DefaultInternalRedirectPolicyIsSensible) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          internal_redirect_policy: {}
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const auto& internal_redirect_policy =
      config.route(headers, 0)->routeEntry()->internalRedirectPolicy();
  EXPECT_TRUE(internal_redirect_policy.enabled());
  EXPECT_TRUE(internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(302)));
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(200)));
  EXPECT_EQ(1, internal_redirect_policy.maxInternalRedirects());
  EXPECT_TRUE(internal_redirect_policy.predicates().empty());
  EXPECT_FALSE(internal_redirect_policy.isCrossSchemeRedirectAllowed());
}

TEST_F(RouteConfigurationV2, InternalRedirectPolicyDropsInvalidRedirectCode) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          internal_redirect_policy:
            redirect_response_codes: [301, 302, 303, 304]
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const auto& internal_redirect_policy =
      config.route(headers, 0)->routeEntry()->internalRedirectPolicy();
  EXPECT_TRUE(internal_redirect_policy.enabled());
  EXPECT_TRUE(internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(301)));
  EXPECT_TRUE(internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(302)));
  EXPECT_TRUE(internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(303)));
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(304)));
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(305)));
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(306)));
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(307)));
}

TEST_F(RouteConfigurationV2, InternalRedirectPolicyDropsInvalidRedirectCodeCauseEmptySet) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: regex
    domains: [idle.lyft.com]
    routes:
      - match:
          safe_regex:
            google_re2: {}
            regex: "/regex"
        route:
          cluster: some-cluster
          internal_redirect_policy:
            redirect_response_codes: [200, 304]
  )EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  Http::TestRequestHeaderMapImpl headers =
      genRedirectHeaders("idle.lyft.com", "/regex", true, false);
  const auto& internal_redirect_policy =
      config.route(headers, 0)->routeEntry()->internalRedirectPolicy();
  EXPECT_TRUE(internal_redirect_policy.enabled());
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(302)));
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(304)));
  EXPECT_FALSE(
      internal_redirect_policy.shouldRedirectForResponseCode(static_cast<Http::Code>(200)));
}

class PerFilterConfigsTest : public testing::Test, public ConfigImplTestBase {
public:
  PerFilterConfigsTest()
      : registered_factory_(factory_), registered_default_factory_(default_factory_) {}

  struct DerivedFilterConfig : public RouteSpecificFilterConfig {
    ProtobufWkt::Timestamp config_;
  };
  class TestFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
  public:
    TestFilterConfig() : EmptyHttpFilterConfig("test.filter") {}

    Http::FilterFactoryCb createFilter(const std::string&,
                                       Server::Configuration::FactoryContext&) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
      return ProtobufTypes::MessagePtr{new ProtobufWkt::Timestamp()};
    }
    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      // Override this to guarantee that we have a different factory mapping by-type.
      return ProtobufTypes::MessagePtr{new ProtobufWkt::Timestamp()};
    }
    Router::RouteSpecificFilterConfigConstSharedPtr
    createRouteSpecificFilterConfig(const Protobuf::Message& message,
                                    Server::Configuration::ServerFactoryContext&,
                                    ProtobufMessage::ValidationVisitor&) override {
      auto obj = std::make_shared<DerivedFilterConfig>();
      obj->config_.MergeFrom(message);
      return obj;
    }
  };
  class DefaultTestFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
  public:
    DefaultTestFilterConfig() : EmptyHttpFilterConfig("test.default.filter") {}

    Http::FilterFactoryCb createFilter(const std::string&,
                                       Server::Configuration::FactoryContext&) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
      return ProtobufTypes::MessagePtr{new ProtobufWkt::Struct()};
    }
  };

  void checkEach(const std::string& yaml, uint32_t expected_entry, uint32_t expected_route,
                 uint32_t expected_vhost) {
    const TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

    const auto route = config.route(genHeaders("www.foo.com", "/", "GET"), 0);
    const auto* route_entry = route->routeEntry();
    const auto& vhost = route_entry->virtualHost();

    check(route_entry->perFilterConfigTyped<DerivedFilterConfig>(factory_.name()), expected_entry,
          "route entry");
    check(route->perFilterConfigTyped<DerivedFilterConfig>(factory_.name()), expected_route,
          "route");
    check(vhost.perFilterConfigTyped<DerivedFilterConfig>(factory_.name()), expected_vhost,
          "virtual host");
  }

  void check(const DerivedFilterConfig* cfg, uint32_t expected_seconds, std::string source) {
    EXPECT_NE(nullptr, cfg) << "config should not be null for source: " << source;
    EXPECT_EQ(expected_seconds, cfg->config_.seconds())
        << "config value does not match expected for source: " << source;
  }

  void checkNoPerFilterConfig(const std::string& yaml) {
    const TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);

    const auto route = config.route(genHeaders("www.foo.com", "/", "GET"), 0);
    const auto* route_entry = route->routeEntry();
    const auto& vhost = route_entry->virtualHost();

    EXPECT_EQ(nullptr,
              route_entry->perFilterConfigTyped<DerivedFilterConfig>(default_factory_.name()));
    EXPECT_EQ(nullptr, route->perFilterConfigTyped<DerivedFilterConfig>(default_factory_.name()));
    EXPECT_EQ(nullptr, vhost.perFilterConfigTyped<DerivedFilterConfig>(default_factory_.name()));
  }

  TestFilterConfig factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registered_factory_;
  DefaultTestFilterConfig default_factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>
      registered_default_factory_;
};

TEST_F(PerFilterConfigsTest, DEPRECATED_FEATURE_TEST(TypedConfigFilterError)) {
  {
    const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
    per_filter_config: { unknown.filter: {} }
    typed_per_filter_config:
      unknown.filter:
        "@type": type.googleapis.com/google.protobuf.Timestamp
)EOF";

    EXPECT_THROW_WITH_MESSAGE(
        TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
        EnvoyException, "Only one of typed_configs or configs can be specified");
  }

  {
    const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
        per_filter_config: { unknown.filter: {} }
        typed_per_filter_config:
          unknown.filter:
            "@type": type.googleapis.com/google.protobuf.Timestamp
)EOF";

    EXPECT_THROW_WITH_MESSAGE(
        TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true),
        EnvoyException, "Only one of typed_configs or configs can be specified");
  }
}

TEST_F(PerFilterConfigsTest, DEPRECATED_FEATURE_TEST(UnknownFilterStruct)) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
    per_filter_config: { unknown.filter: {} }
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Didn't find a registered implementation for name: 'unknown.filter'");
}

TEST_F(PerFilterConfigsTest, UnknownFilterAny) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
    typed_per_filter_config:
      unknown.filter:
        "@type": type.googleapis.com/google.protobuf.Timestamp
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      TestConfigImpl(parseRouteConfigurationFromYaml(yaml), factory_context_, true), EnvoyException,
      "Didn't find a registered implementation for name: 'unknown.filter'");
}

// Test that a trivially specified NamedHttpFilterConfigFactory ignores per_filter_config without
// error.
TEST_F(PerFilterConfigsTest, DEPRECATED_FEATURE_TEST(DefaultFilterImplementationStruct)) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
    per_filter_config: { test.default.filter: { seconds: 123} }
)EOF";

  checkNoPerFilterConfig(yaml);
}

TEST_F(PerFilterConfigsTest, DefaultFilterImplementationAny) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
    typed_per_filter_config:
      test.default.filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value:
          seconds: 123
)EOF";

  checkNoPerFilterConfig(yaml);
}

TEST_F(PerFilterConfigsTest, DEPRECATED_FEATURE_TEST(RouteLocalConfig)) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
        per_filter_config: { test.filter: { seconds: 123 } }
    per_filter_config: { test.filter: { seconds: 456 } }
)EOF";

  checkEach(yaml, 123, 123, 456);
}

TEST_F(PerFilterConfigsTest, RouteLocalTypedConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
        typed_per_filter_config:
          test.filter:
            "@type": type.googleapis.com/google.protobuf.Timestamp
            value:
              seconds: 123
    typed_per_filter_config:
      test.filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value:
          seconds: 456
)EOF";

  checkEach(yaml, 123, 123, 456);
}

TEST_F(PerFilterConfigsTest, DEPRECATED_FEATURE_TEST(WeightedClusterConfig)) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            clusters:
              - name: baz
                weight: 100
                per_filter_config: { test.filter: { seconds: 789 } }
    per_filter_config: { test.filter: { seconds: 1011 } }
)EOF";

  checkEach(yaml, 789, 789, 1011);
}

TEST_F(PerFilterConfigsTest, WeightedClusterTypedConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            clusters:
              - name: baz
                weight: 100
                typed_per_filter_config:
                  test.filter:
                    "@type": type.googleapis.com/google.protobuf.Timestamp
                    value:
                      seconds: 789
    typed_per_filter_config:
      test.filter:
        "@type": type.googleapis.com/google.protobuf.Timestamp
        value:
          seconds: 1011
)EOF";

  checkEach(yaml, 789, 789, 1011);
}

TEST_F(PerFilterConfigsTest, DEPRECATED_FEATURE_TEST(WeightedClusterFallthroughConfig)) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            clusters:
              - name: baz
                weight: 100
        per_filter_config: { test.filter: { seconds: 1213 } }
    per_filter_config: { test.filter: { seconds: 1415 } }
)EOF";

  checkEach(yaml, 1213, 1213, 1415);
}

TEST_F(PerFilterConfigsTest, WeightedClusterFallthroughTypedConfig) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route:
          weighted_clusters:
            clusters:
              - name: baz
                weight: 100
        typed_per_filter_config:
          test.filter:
            "@type": type.googleapis.com/google.protobuf.Timestamp
            value:
              seconds: 1213
    typed_per_filter_config:
      test.filter:
        "@type": type.googleapis.com/google.protobuf.Timestamp
        value:
          seconds: 1415
)EOF";

  checkEach(yaml, 1213, 1213, 1415);
}

class RouteMatchOverrideTest : public testing::Test, public ConfigImplTestBase {};

TEST_F(RouteMatchOverrideTest, VerifyAllMatchableRoutes) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/foo" }
        route:
          cluster: foo
      - match: { prefix: "/" }
        route:
          cluster: default
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  std::vector<std::string> clusters{"default", "foo", "foo_bar", "foo_bar_baz"};

  RouteConstSharedPtr accepted_route = config.route(
      [&clusters](RouteConstSharedPtr route,
                  RouteEvalStatus route_eval_status) -> RouteMatchStatus {
        EXPECT_FALSE(clusters.empty());
        EXPECT_EQ(clusters[clusters.size() - 1], route->routeEntry()->clusterName());
        clusters.pop_back();
        if (clusters.empty()) {
          EXPECT_EQ(route_eval_status, RouteEvalStatus::NoMoreRoutes);
          return RouteMatchStatus::Accept;
        }
        EXPECT_EQ(route_eval_status, RouteEvalStatus::HasMoreRoutes);
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/foo/bar/baz", "GET"));
  EXPECT_EQ(accepted_route->routeEntry()->clusterName(), "default");
}

TEST_F(RouteMatchOverrideTest, VerifyRouteOverrideStops) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/foo" }
        route:
          cluster: foo
      - match: { prefix: "/" }
        route:
          cluster: default
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  std::vector<std::string> clusters{"foo", "foo_bar"};

  RouteConstSharedPtr accepted_route = config.route(
      [&clusters](RouteConstSharedPtr route,
                  RouteEvalStatus route_eval_status) -> RouteMatchStatus {
        EXPECT_FALSE(clusters.empty());
        EXPECT_EQ(clusters[clusters.size() - 1], route->routeEntry()->clusterName());
        clusters.pop_back();
        EXPECT_EQ(route_eval_status, RouteEvalStatus::HasMoreRoutes);

        if (clusters.empty()) {
          return RouteMatchStatus::Accept; // Do not match default route
        }
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/foo/bar", "GET"));
  EXPECT_EQ(accepted_route->routeEntry()->clusterName(), "foo");
}

TEST_F(RouteMatchOverrideTest, StopWhenNoMoreRoutes) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/foo" }
        route:
          cluster: foo
      - match: { prefix: "/" }
        route:
          cluster: default
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  std::vector<std::string> clusters{"default", "foo", "foo_bar", "foo_bar_baz"};

  RouteConstSharedPtr accepted_route = config.route(
      [&clusters](RouteConstSharedPtr route,
                  RouteEvalStatus route_eval_status) -> RouteMatchStatus {
        EXPECT_FALSE(clusters.empty());
        EXPECT_EQ(clusters[clusters.size() - 1], route->routeEntry()->clusterName());
        clusters.pop_back();

        if (clusters.empty()) {
          EXPECT_EQ(route_eval_status, RouteEvalStatus::NoMoreRoutes);
        } else {
          EXPECT_EQ(route_eval_status, RouteEvalStatus::HasMoreRoutes);
        }
        // Returning continue when no more routes are available will be ignored by ConfigImpl::route
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/foo/bar/baz", "GET"));
  EXPECT_EQ(accepted_route, nullptr);
}

TEST_F(RouteMatchOverrideTest, NullRouteOnNoRouteMatch) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/foo" }
        route:
          cluster: foo
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  RouteConstSharedPtr accepted_route = config.route(
      [](RouteConstSharedPtr, RouteEvalStatus) -> RouteMatchStatus {
        ADD_FAILURE()
            << "RouteCallback should not be invoked since there are no matching route to override";
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/", "GET"));
  EXPECT_EQ(accepted_route, nullptr);
}

TEST_F(RouteMatchOverrideTest, NullRouteOnNoHostMatch) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["www.acme.com"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/" }
        route:
          cluster: default
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  RouteConstSharedPtr accepted_route = config.route(
      [](RouteConstSharedPtr, RouteEvalStatus) -> RouteMatchStatus {
        ADD_FAILURE()
            << "RouteCallback should not be invoked since there are no matching route to override";
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/", "GET"));
  EXPECT_EQ(accepted_route, nullptr);
}

TEST_F(RouteMatchOverrideTest, NullRouteOnNullXForwardedProto) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/" }
        route:
          cluster: default
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  RouteConstSharedPtr accepted_route = config.route(
      [](RouteConstSharedPtr, RouteEvalStatus) -> RouteMatchStatus {
        ADD_FAILURE()
            << "RouteCallback should not be invoked since there are no matching route to override";
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/", "GET", ""));
  EXPECT_EQ(accepted_route, nullptr);
}

TEST_F(RouteMatchOverrideTest, NullRouteOnRequireTlsAll) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/" }
        route:
          cluster: default
    require_tls: ALL
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  RouteConstSharedPtr accepted_route = config.route(
      [](RouteConstSharedPtr, RouteEvalStatus) -> RouteMatchStatus {
        ADD_FAILURE()
            << "RouteCallback should not be invoked since there are no matching route to override";
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/", "GET"));
  EXPECT_NE(nullptr, dynamic_cast<const SslRedirectRoute*>(accepted_route.get()));
}

TEST_F(RouteMatchOverrideTest, NullRouteOnRequireTlsInternal) {
  const std::string yaml = R"EOF(
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/foo/bar/baz" }
        route:
          cluster: foo_bar_baz
      - match: { prefix: "/foo/bar" }
        route:
          cluster: foo_bar
      - match: { prefix: "/" }
        route:
          cluster: default
    require_tls: EXTERNAL_ONLY
)EOF";

  TestConfigImpl config(parseRouteConfigurationFromYaml(yaml), factory_context_, true);
  RouteConstSharedPtr accepted_route = config.route(
      [](RouteConstSharedPtr, RouteEvalStatus) -> RouteMatchStatus {
        ADD_FAILURE()
            << "RouteCallback should not be invoked since there are no matching route to override";
        return RouteMatchStatus::Continue;
      },
      genHeaders("bat.com", "/", "GET"));
  EXPECT_NE(nullptr, dynamic_cast<const SslRedirectRoute*>(accepted_route.get()));
}

} // namespace
} // namespace Router
} // namespace Envoy
