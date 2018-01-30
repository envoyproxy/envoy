#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "common/config/metadata.h"
#include "common/config/rds_json.h"
#include "common/config/well_known_names.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/router/config_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::ElementsAreArray;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrNe;
using testing::_;

namespace Envoy {
namespace Router {
namespace {

Http::TestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                   const std::string& method) {
  return Http::TestHeaderMapImpl{{":authority", host}, {":path", path}, {":method", method}};
}

envoy::api::v2::RouteConfiguration parseRouteConfigurationFromJson(const std::string& json_string) {
  envoy::api::v2::RouteConfiguration route_config;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRouteConfiguration(*json_object_ptr, route_config);
  return route_config;
}

envoy::api::v2::RouteConfiguration parseRouteConfigurationFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::RouteConfiguration route_config;
  MessageUtil::loadFromYaml(yaml, route_config);
  return route_config;
}

TEST(RouteMatcherTest, TestRoutes) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["lyft.com", "www.lyft.com", "w.lyft.com", "ww.lyft.com", "wwww.lyft.com"],
      "routes": [
        {
          "prefix": "/new_endpoint",
          "prefix_rewrite": "/api/new_endpoint",
          "cluster": "www2"
        },
        {
          "path": "/",
          "cluster": "root_www2"
        },
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["www-staging.lyft.net", "www-staging-orca.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging"
        }
      ]
    },
    {
      "name": "wildcard",
      "domains": ["*.foo.com", "*-bar.baz.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "wildcard"
        }
      ]
    },
    {
      "name": "wildcard2",
      "domains": ["*.baz.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "wildcard2"
        }
      ]
    },
    {
      "name": "regex",
      "domains": ["bat.com"],
      "routes": [
        {
          "regex": "/t[io]c",
          "cluster": "clock"
        },
        {
          "regex": "/baa+",
          "cluster": "sheep"
        },
        {
          "regex": ".*/\\d{3}$",
          "cluster": "three_numbers",
          "prefix_rewrite": "/rewrote"
        },
        {
          "regex": ".*",
          "cluster": "regex_default"
        }
      ]
    },
    {
      "name": "regex2",
      "domains": ["bat2.com"],
      "routes": [
        {
          "regex": "",
          "cluster": "nothingness"
        },
        {
          "regex": ".*",
          "cluster": "regex_default"
        }
      ]
    },
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api/application_data",
          "cluster": "ats"
        },
        {
          "path": "/api/locations",
          "cluster": "locations",
          "prefix_rewrite": "/rewrote",
          "case_sensitive": false
        },
        {
          "prefix": "/api/leads/me",
          "cluster": "ats"
        },
        {
          "prefix": "/host/rewrite/me",
          "cluster": "ats",
          "host_rewrite": "new_host"
        },
        {
          "prefix": "/oldhost/rewrite/me",
          "cluster": "ats",
          "host_rewrite": "new_oldhost"
        },
        {
          "path": "/foo",
          "prefix_rewrite": "/bar",
          "cluster": "instant-server",
          "case_sensitive": true
        },
        {
          "path": "/tar",
          "prefix_rewrite": "/car",
          "cluster": "instant-server",
          "case_sensitive": false
        },
        {
          "prefix": "/newhost/rewrite/me",
          "cluster": "ats",
          "host_rewrite": "new_host",
          "case_sensitive": false
        },
        {
          "path": "/FOOD",
          "prefix_rewrite": "/cAndy",
          "cluster": "ats",
          "case_sensitive":false
        },
        {
          "path": "/ApplEs",
          "prefix_rewrite": "/oranGES",
          "cluster": "instant-server",
          "case_sensitive": true
        },
        {
          "prefix": "/",
          "cluster": "instant-server",
          "timeout_ms": 30000
        }],
      "virtual_clusters": [
        {"pattern": "^/rides$", "method": "POST", "name": "ride_request"},
        {"pattern": "^/rides/\\d+$", "method": "PUT", "name": "update_ride"},
        {"pattern": "^/users/\\d+/chargeaccounts$", "method": "POST", "name": "cc_add"},
        {"pattern": "^/users/\\d+/chargeaccounts/(?!validate)\\w+$", "method": "PUT",
         "name": "cc_add"},
        {"pattern": "^/users$", "method": "POST", "name": "create_user_login"},
        {"pattern": "^/users/\\d+$", "method": "PUT", "name": "update_user"},
        {"pattern": "^/users/\\d+/location$", "method": "POST", "name": "ulu"}]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

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
  EXPECT_EQ("nothingness",
            config.route(genHeaders("bat2.com", "", "GET"), 0)->routeEntry()->clusterName());
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
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", route->virtualHost().name());
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/api/new_endpoint/foo", headers.get_(Http::Headers::get().Path));
  }

  // Prefix rewrite on path match with query string params
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/api/locations?works=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?works=true", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/bar", headers.get_(Http::Headers::get().Path));
  }

  // Host rewrite testing.
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/host/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("new_host", headers.get_(Http::Headers::get().Host));
  }

  // Case sensitive rewrite matching test.
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/API/locations?works=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?works=true", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/fooD", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/cAndy", headers.get_(Http::Headers::get().Path));
  }

  // Case sensitive is set to true and will not rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/FOO", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/FOO", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/ApPles", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/ApPles", headers.get_(Http::Headers::get().Path));
  }

  // Case insensitive set to false so there is no rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/oLDhost/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("api.lyft.com", headers.get_(Http::Headers::get().Host));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/Tart", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/Tart", headers.get_(Http::Headers::get().Path));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/newhost/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("new_host", headers.get_(Http::Headers::get().Host));
  }

  // Prefix rewrite for regular expression matching
  {
    Http::TestHeaderMapImpl headers = genHeaders("bat.com", "/647", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote", headers.get_(Http::Headers::get().Path));
  }

  // Prefix rewrite for regular expression matching with query string
  {
    Http::TestHeaderMapImpl headers = genHeaders("bat.com", "/970?foo=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?foo=true", headers.get_(Http::Headers::get().Path));
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("bat.com", "/foo/bar/238?bar=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?bar=true", headers.get_(Http::Headers::get().Path));
  }

  // Virtual cluster testing.
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "GET");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/blah", "POST");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "POST");
    EXPECT_EQ("ride_request",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123", "PUT");
    EXPECT_EQ("update_ride",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123/456", "POST");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts", "POST");
    EXPECT_EQ("cc_add", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/hello123", "PUT");
    EXPECT_EQ("cc_add", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/validate", "PUT");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/foo/bar", "PUT");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users", "POST");
    EXPECT_EQ("create_user_login",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123", "PUT");
    EXPECT_EQ("update_user",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123/location", "POST");
    EXPECT_EQ("ulu", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/something/else", "GET");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
}

TEST(RouteMatcherTest, TestRoutesWithInvalidRegex) {
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

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;

  EXPECT_THROW_WITH_REGEX(
      ConfigImpl(parseRouteConfigurationFromV2Yaml(invalid_route), runtime, cm, true),
      EnvoyException, "Invalid regex '/\\(\\+invalid\\)':");

  EXPECT_THROW_WITH_REGEX(
      ConfigImpl(parseRouteConfigurationFromV2Yaml(invalid_virtual_cluster), runtime, cm, true),
      EnvoyException, "Invalid regex '\\^/\\(\\+invalid\\)':");
}

// Validates behavior of request_headers_to_add at router, vhost, and route levels.
TEST(RouteMatcherTest, TestAddRemoveRequestHeaders) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["lyft.com", "www.lyft.com", "w.lyft.com", "ww.lyft.com", "wwww.lyft.com"],
      "request_headers_to_add": [
          {"key": "x-global-header1", "value": "vhost-override"},
          {"key": "x-vhost-header1", "value": "vhost1-www2"}
      ],
      "routes": [
        {
          "prefix": "/new_endpoint",
          "prefix_rewrite": "/api/new_endpoint",
          "cluster": "www2",
          "request_headers_to_add": [
             {"key": "x-global-header1", "value": "route-override"},
             {"key": "x-vhost-header1", "value": "route-override"},
             {"key": "x-route-header", "value": "route-new_endpoint"}
          ]
        },
        {
          "path": "/",
          "cluster": "root_www2",
          "request_headers_to_add": [
             {"key": "x-route-header", "value": "route-allpath"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["www-staging.lyft.net", "www-staging-orca.lyft.com"],
      "request_headers_to_add": [
          {"key": "x-vhost-header1", "value": "vhost1-www2_staging"}
      ],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging",
          "request_headers_to_add": [
             {"key": "x-route-header", "value": "route-allprefix"}
          ]
        }
      ]
    },
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "instant-server",
          "timeout_ms": 30000
        }
      ]
    }
  ],

  "internal_only_headers": [
    "x-lyft-user-id"
  ],

  "response_headers_to_add": [
    {"key": "x-envoy-upstream-canary", "value": "true"}
  ],

  "response_headers_to_remove": [
    "x-envoy-upstream-canary",
    "x-envoy-virtual-cluster"
  ],

  "request_headers_to_add": [
    {"key": "x-global-header1", "value": "global1"}
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;

  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  // Request header manipulation testing.
  {
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("route-override", headers.get_("x-global-header1"));
      EXPECT_EQ("route-override", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-new_endpoint", headers.get_("x-route-header"));
    }

    // Multiple routes can have same route-level headers with different values.
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("vhost-override", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allpath", headers.get_("x-route-header"));
    }

    // Multiple virtual hosts can have same virtual host level headers with different values.
    {
      Http::TestHeaderMapImpl headers = genHeaders("www-staging.lyft.net", "/foo", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2_staging", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allprefix", headers.get_("x-route-header"));
    }

    // Global headers.
    {
      Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
    }
  }
}

// Validates behavior of request_headers_to_add at router, vhost, and route levels when append
// is disabled.
TEST(RouteMatcherTest, TestRequestHeadersToAddWithAppendFalse) {
  std::string yaml = R"EOF(
name: foo
virtual_hosts:
  - name: www2
    domains: ["*"]
    request_headers_to_add:
      - header:
          key: x-global-header
          value: vhost-www2
        append: false
      - header:
          key: x-vhost-header
          value: vhost-www2
        append: false
    routes:
      - match: { prefix: "/endpoint" }
        route:
          cluster: www2
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
      - match: { prefix: "/" }
        route: { cluster: www2 }
request_headers_to_add:
  - header:
      key: x-global-header
      value: global
    append: false
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;

  envoy::api::v2::RouteConfiguration route_config = parseRouteConfigurationFromV2Yaml(yaml);

  ConfigImpl config(route_config, runtime, cm, true);

  // Request header manipulation testing.
  {
    // Global and virtual host override route.
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/endpoint", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("global", headers.get_("x-global-header"));
      EXPECT_EQ("vhost-www2", headers.get_("x-vhost-header"));
      EXPECT_EQ("route-endpoint", headers.get_("x-route-header"));
    }

    // Global overrides virtual host.
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("global", headers.get_("x-global-header"));
      EXPECT_EQ("vhost-www2", headers.get_("x-vhost-header"));
    }
  }
}

// Validates behavior of response_headers_to_add and response_headers_to_remove at router, vhost,
// and route levels.
TEST(RouteMatcherTest, TestAddRemoveResponseHeaders) {
  std::string yaml = R"EOF(
name: foo
virtual_hosts:
  - name: www2
    domains: ["www.lyft.com"]
    response_headers_to_add:
      - header:
          key: x-global-header1
          value: vhost-override
      - header:
          key: x-vhost-header1
          value: vhost1-www2
    response_headers_to_remove: ["x-vhost-remove"]
    routes:
      - match: { prefix: "/new_endpoint" }
        route:
          prefix_rewrite: "/api/new_endpoint"
          cluster: www2
          response_headers_to_add:
            - header:
                key: x-global-header1
                value: route-override
            - header:
                key: x-vhost-header1
                value: route-override
            - header:
                key: x-route-header
                value: route-new_endpoint
      - match: { path: "/" }
        route:
          cluster: root_www2
          response_headers_to_add:
            - header:
                key: x-route-header
                value: route-allpath
          response_headers_to_remove: ["x-route-remove"]
      - match: { prefix: "/" }
        route: { cluster: "www2" }
  - name: www2_staging
    domains: ["www-staging.lyft.net"]
    response_headers_to_add:
      - header:
          key: x-vhost-header1
          value: vhost1-www2_staging
    routes:
      - match: { prefix: "/" }
        route:
          cluster: www2_staging
          response_headers_to_add:
            - header:
                key: x-route-header
                value: route-allprefix
  - name: default
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: "instant-server" }
internal_only_headers: ["x-lyft-user-id"]
response_headers_to_add:
  - header:
      key: x-global-header1
      value: global1
response_headers_to_remove: ["x-global-remove"]
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;

  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true);

  // Response header manipulation testing.
  {
    {
      Http::TestHeaderMapImpl req_headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, request_info);
      EXPECT_EQ("route-override", headers.get_("x-global-header1"));
      EXPECT_EQ("route-override", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-new_endpoint", headers.get_("x-route-header"));
    }

    // Multiple routes can have same route-level headers with different values.
    {
      Http::TestHeaderMapImpl req_headers = genHeaders("www.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, request_info);
      EXPECT_EQ("vhost-override", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allpath", headers.get_("x-route-header"));
    }

    // Multiple virtual hosts can have same virtual host level headers with different values.
    {
      Http::TestHeaderMapImpl req_headers = genHeaders("www-staging.lyft.net", "/foo", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, request_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2_staging", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allprefix", headers.get_("x-route-header"));
    }

    // Global headers.
    {
      Http::TestHeaderMapImpl req_headers = genHeaders("api.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(req_headers, 0)->routeEntry();
      Http::TestHeaderMapImpl headers;
      route->finalizeResponseHeaders(headers, request_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
    }
  }

  EXPECT_THAT(std::list<Http::LowerCaseString>{Http::LowerCaseString("x-lyft-user-id")},
              ContainerEq(config.internalOnlyHeaders()));
}

TEST(RouteMatcherTest, Priority) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "local_service_grpc",
          "priority": "high"
        },
        {
          "prefix": "/bar",
          "cluster": "local_service_grpc"
        }
      ],
      "virtual_clusters": [
        {"pattern": "^/bar$", "method": "POST", "name": "foo"}]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_EQ(Upstream::ResourcePriority::High,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)->routeEntry()->priority());
  EXPECT_EQ(Upstream::ResourcePriority::Default,
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry()->priority());
}

TEST(RouteMatcherTest, NoHostRewriteAndAutoRewrite) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "local_service",
          "host_rewrite": "foo",
          "auto_host_rewrite" : true
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, NoRedirectAndWebSocket) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com",
          "use_websocket": true
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, HeaderMatchedRouting) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "local_service_with_headers",
          "headers" : [
            {"name": "test_header", "value": "test"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_multiple_headers",
          "headers" : [
            {"name": "test_header_multiple1", "value": "test1"},
            {"name": "test_header_multiple2", "value": "test2"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_empty_headers",
          "headers" : [
            {"name": "test_header_presence"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_header_pattern_set_regex",
          "headers" : [
            {"name": "test_header_pattern", "value": "^user=test-\\d+$", "regex": true}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_header_pattern_unset_regex",
          "headers" : [
            {"name": "test_header_pattern", "value": "^customer=test-\\d+$"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_without_headers"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    EXPECT_EQ("local_service_without_headers",
              config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header", "test");
    EXPECT_EQ("local_service_with_headers", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_multiple1", "test1");
    headers.addCopy("test_header_multiple2", "test2");
    EXPECT_EQ("local_service_with_multiple_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("non_existent_header", "foo");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_presence", "test");
    EXPECT_EQ("local_service_with_empty_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "user=test-1223");
    EXPECT_EQ("local_service_with_header_pattern_set_regex",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "customer=test-1223");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
}

// Verify the fixes for https://github.com/envoyproxy/envoy/issues/2406
TEST(RouteMatcherTest, InvalidHeaderMatchedRoutingConfig) {
  std::string value_with_regex_chars = R"EOF(
virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
      - match:
          prefix: "/"
          headers:
            - name: test_header
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
          headers:
            - name: test_header
              value: "(+invalid regex)"
              regex: true
        route: { cluster: "local_service" }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_NO_THROW(
      ConfigImpl(parseRouteConfigurationFromV2Yaml(value_with_regex_chars), runtime, cm, true));

  EXPECT_THROW_WITH_REGEX(
      ConfigImpl(parseRouteConfigurationFromV2Yaml(invalid_regex), runtime, cm, true),
      EnvoyException, "Invalid regex");
}

TEST(RouteMatcherTest, QueryParamMatchedRouting) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "local_service_with_multiple_query_parameters",
          "query_parameters": [
            {"name": "id", "value": "\\d+[02468]", "regex": true},
            {"name": "debug"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_query_parameter",
          "query_parameters": [
            {"name": "param", "value": "test"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_valueless_query_parameter",
          "query_parameters": [
            {"name": "debug"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_without_query_parameters"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/", "GET");
    EXPECT_EQ("local_service_without_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/?", "GET");
    EXPECT_EQ("local_service_without_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/?param=testing", "GET");
    EXPECT_EQ("local_service_without_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/?param=test", "GET");
    EXPECT_EQ("local_service_with_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/?debug", "GET");
    EXPECT_EQ("local_service_with_valueless_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/?debug=2", "GET");
    EXPECT_EQ("local_service_with_valueless_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/?param=test&debug&id=01", "GET");
    EXPECT_EQ("local_service_with_query_parameter",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("example.com", "/?param=test&debug&id=02", "GET");
    EXPECT_EQ("local_service_with_multiple_query_parameters",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
}

// Verify the fixes for https://github.com/envoyproxy/envoy/issues/2406
TEST(RouteMatcherTest, InvalidQueryParamMatchedRoutingConfig) {
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

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_NO_THROW(
      ConfigImpl(parseRouteConfigurationFromV2Yaml(value_with_regex_chars), runtime, cm, true));

  EXPECT_THROW_WITH_REGEX(
      ConfigImpl(parseRouteConfigurationFromV2Yaml(invalid_regex), runtime, cm, true),
      EnvoyException, "Invalid regex");
}

class RouterMatcherHashPolicyTest : public testing::Test {
public:
  RouterMatcherHashPolicyTest()
      : add_cookie_nop_([](const std::string&, std::chrono::seconds) { return ""; }) {
    std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "foo"
        },
        {
          "prefix": "/bar",
          "cluster": "bar"
        }
      ]
    }
  ]
}
  )EOF";
    route_config_ = parseRouteConfigurationFromJson(json);
  }

  envoy::api::v2::route::RouteAction_HashPolicy* firstRouteHashPolicy() {
    auto hash_policies = route_config_.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_hash_policy();
    if (hash_policies->size() > 0) {
      return hash_policies->Mutable(0);
    } else {
      return hash_policies->Add();
    }
  }

  ConfigImpl& config() {
    if (config_ == nullptr) {
      config_ = std::unique_ptr<ConfigImpl>{new ConfigImpl(route_config_, runtime_, cm_, true)};
    }
    return *config_;
  }

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  envoy::api::v2::RouteConfiguration route_config_;
  HashPolicy::AddCookieCallback add_cookie_nop_;

private:
  std::unique_ptr<ConfigImpl> config_;
};

TEST_F(RouterMatcherHashPolicyTest, HashHeaders) {
  firstRouteHashPolicy()->mutable_header()->set_header_name("foo_header");
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(
        route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(
        route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
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
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(
        route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).valid());
  }
  {
    // With no matching cookie, no hash is generated.
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; su=gar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(
        route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).valid());
  }
  {
    // Matching cookie produces a valid hash.
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; hash=brown");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(
        route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).valid());
  }
  {
    // The hash policy is per-route.
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherCookieHashPolicyTest, DifferentCookies) {
  // Different cookies produce different hashes.
  uint64_t hash_1, hash_2;
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "hash=brown");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_1 = route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).value();
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "hash=green");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_2 = route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).value();
  }
  EXPECT_NE(hash_1, hash_2);
}

TEST_F(RouterMatcherCookieHashPolicyTest, TtlSet) {
  firstRouteHashPolicy()->mutable_cookie()->mutable_ttl()->set_seconds(42);

  MockFunction<std::string(const std::string&, long)> mock_cookie_cb;
  auto add_cookie = [&mock_cookie_cb](const std::string& name,
                                      std::chrono::seconds ttl) -> std::string {
    return mock_cookie_cb.Call(name, ttl.count());
  };

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_CALL(mock_cookie_cb, Call("hash", 42));
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; su=gar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_CALL(mock_cookie_cb, Call("hash", 42));
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("Cookie", "choco=late; hash=brown");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie).valid());
  }
  {
    uint64_t hash_1, hash_2;
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
      Router::RouteConstSharedPtr route = config().route(headers, 0);
      EXPECT_CALL(mock_cookie_cb, Call("hash", 42)).WillOnce(Return("AAAAAAA"));
      hash_1 = route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie).value();
    }
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
      Router::RouteConstSharedPtr route = config().route(headers, 0);
      EXPECT_CALL(mock_cookie_cb, Call("hash", 42)).WillOnce(Return("BBBBBBB"));
      hash_2 = route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie).value();
    }
    EXPECT_NE(hash_1, hash_2);
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashIp) {
  firstRouteHashPolicy()->mutable_connection_properties()->set_source_ip(true);
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(
        route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_TRUE(route->routeEntry()
                    ->hashPolicy()
                    ->generateHash("1.2.3.4", headers, add_cookie_nop_)
                    .valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    uint64_t old_hash = config()
                            .route(headers, 0)
                            ->routeEntry()
                            ->hashPolicy()
                            ->generateHash("1.2.3.4", headers, add_cookie_nop_)
                            .value();
    headers.addCopy("foo_header", "bar");
    EXPECT_EQ(old_hash, config()
                            .route(headers, 0)
                            ->routeEntry()
                            ->hashPolicy()
                            ->generateHash("1.2.3.4", headers, add_cookie_nop_)
                            .value());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    const auto hash_policy = config().route(headers, 0)->routeEntry()->hashPolicy();
    const uint64_t hash_1 = hash_policy->generateHash("1.2.3.4", headers, add_cookie_nop_).value();
    const uint64_t hash_2 = hash_policy->generateHash("4.3.2.1", headers, add_cookie_nop_).value();
    EXPECT_NE(hash_1, hash_2);
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashMultiple) {
  auto route = route_config_.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
  route->add_hash_policy()->mutable_header()->set_header_name("foo_header");
  route->add_hash_policy()->mutable_connection_properties()->set_source_ip(true);

  uint64_t hash_h, hash_ip, hash_both;
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    EXPECT_FALSE(
        route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_h = route->routeEntry()->hashPolicy()->generateHash("", headers, add_cookie_nop_).value();
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    hash_ip = route->routeEntry()
                  ->hashPolicy()
                  ->generateHash("4.2.1.3", headers, add_cookie_nop_)
                  .value();
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    headers.addCopy("foo_header", "bar");
    hash_both = route->routeEntry()
                    ->hashPolicy()
                    ->generateHash("4.2.1.3", headers, add_cookie_nop_)
                    .value();
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config().route(headers, 0);
    headers.addCopy("foo_header", "bar");
    // stability
    EXPECT_EQ(hash_both, route->routeEntry()
                             ->hashPolicy()
                             ->generateHash("4.2.1.3", headers, add_cookie_nop_)
                             .value());
  }
  EXPECT_NE(hash_ip, hash_h);
  EXPECT_NE(hash_ip, hash_both);
  EXPECT_NE(hash_h, hash_both);
}

TEST_F(RouterMatcherHashPolicyTest, InvalidHashPolicies) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  {
    auto hash_policy = firstRouteHashPolicy();
    EXPECT_EQ(envoy::api::v2::route::RouteAction::HashPolicy::POLICY_SPECIFIER_NOT_SET,
              hash_policy->policy_specifier_case());
    EXPECT_THROW(config(), EnvoyException);
  }
  {
    auto route = route_config_.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
    route->add_hash_policy()->mutable_header()->set_header_name("foo_header");
    route->add_hash_policy()->mutable_connection_properties()->set_source_ip(true);
    auto hash_policy = route->add_hash_policy();
    EXPECT_EQ(envoy::api::v2::route::RouteAction::HashPolicy::POLICY_SPECIFIER_NOT_SET,
              hash_policy->policy_specifier_case());
    EXPECT_THROW(config(), EnvoyException);
  }
}

TEST(RouteMatcherTest, ClusterHeader) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster_header": ":authority"
        },
        {
          "prefix": "/bar",
          "cluster_header": "some_header",
          "timeout_ms": 0
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_EQ(
      "some_cluster",
      config.route(genHeaders("some_cluster", "/foo", "GET"), 0)->routeEntry()->clusterName());

  EXPECT_EQ(
      "", config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry()->clusterName());

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    headers.addCopy("some_header", "some_cluster");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ("some_cluster", route->routeEntry()->clusterName());

    // Make sure things forward and don't crash.
    EXPECT_EQ(std::chrono::milliseconds(0), route->routeEntry()->timeout());
    route->routeEntry()->finalizeRequestHeaders(headers, request_info);
    route->routeEntry()->priority();
    route->routeEntry()->rateLimitPolicy();
    route->routeEntry()->retryPolicy();
    route->routeEntry()->shadowPolicy();
    route->routeEntry()->virtualCluster(headers);
    route->routeEntry()->virtualHost();
    route->routeEntry()->virtualHost().rateLimitPolicy();
  }
}

TEST(RouteMatcherTest, ContentType) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "local_service_grpc",
          "headers" : [
            {"name": "content-type", "value": "application/grpc"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    EXPECT_EQ("local_service",
              config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("content-type", "application/grpc");
    EXPECT_EQ("local_service_grpc", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("content-type", "foo");
    EXPECT_EQ("local_service", config.route(headers, 0)->routeEntry()->clusterName());
  }
}

TEST(RouteMatcherTest, Runtime) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "something_else",
          "runtime": {
            "key": "some_key",
            "default": 50
          }
        },
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  Runtime::MockSnapshot snapshot;

  ON_CALL(runtime, snapshot()).WillByDefault(ReturnRef(snapshot));

  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_CALL(snapshot, featureEnabled("some_key", 50, 10)).WillOnce(Return(true));
  EXPECT_EQ("something_else",
            config.route(genHeaders("www.lyft.com", "/", "GET"), 10)->routeEntry()->clusterName());

  EXPECT_CALL(snapshot, featureEnabled("some_key", 50, 20)).WillOnce(Return(false));
  EXPECT_EQ("www2",
            config.route(genHeaders("www.lyft.com", "/", "GET"), 20)->routeEntry()->clusterName());
}

TEST(RouteMatcherTest, ShadowClusterNotFound) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "shadow": {
            "cluster": "some_cluster"
          },
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  EXPECT_CALL(cm, get("some_cluster")).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, ClusterNotFound) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, ClusterNotFoundNotChecking) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(nullptr));

  ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, false);
}

TEST(RouteMatcherTest, ClusterNotFoundNotCheckingViaConfig) {
  std::string json = R"EOF(
{
  "validate_clusters": false,
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(nullptr));

  ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true);
}

TEST(RouteMatchTest, ClusterNotFoundResponseCode) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/"}
        route:
          cluster: "not_found"
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, false);

  Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");

  EXPECT_EQ("not_found", config.route(headers, 0)->routeEntry()->clusterName());
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            config.route(headers, 0)->routeEntry()->clusterNotFoundResponseCode());
}

TEST(RouteMatchTest, ClusterNotFoundResponseCodeConfig503) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/"}
        route:
          cluster: "not_found"
          cluster_not_found_response_code: SERVICE_UNAVAILABLE
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, false);

  Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");

  EXPECT_EQ("not_found", config.route(headers, 0)->routeEntry()->clusterName());
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            config.route(headers, 0)->routeEntry()->clusterNotFoundResponseCode());
}

TEST(RouteMatchTest, ClusterNotFoundResponseCodeConfig404) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: "www2"
    domains: ["www.lyft.com"]
    routes:
      - match: { prefix: "/"}
        route:
          cluster: "not_found"
          cluster_not_found_response_code: NOT_FOUND
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, false);

  Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");

  EXPECT_EQ("not_found", config.route(headers, 0)->routeEntry()->clusterName());
  EXPECT_EQ(Http::Code::NotFound,
            config.route(headers, 0)->routeEntry()->clusterNotFoundResponseCode());
}

TEST(RouteMatcherTest, Shadow) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "shadow": {
            "cluster": "some_cluster"
          },
          "cluster": "www2"
        },
        {
          "prefix": "/bar",
          "shadow": {
            "cluster": "some_cluster2",
            "runtime_key": "foo"
          },
          "cluster": "www2"
        },
        {
          "prefix": "/baz",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_EQ("some_cluster", config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                                ->routeEntry()
                                ->shadowPolicy()
                                .cluster());
  EXPECT_EQ("", config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->shadowPolicy()
                    .runtimeKey());

  EXPECT_EQ("some_cluster2", config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                                 ->routeEntry()
                                 ->shadowPolicy()
                                 .cluster());
  EXPECT_EQ("foo", config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                       ->routeEntry()
                       ->shadowPolicy()
                       .runtimeKey());

  EXPECT_EQ("", config.route(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                    ->routeEntry()
                    ->shadowPolicy()
                    .cluster());
  EXPECT_EQ("", config.route(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                    ->routeEntry()
                    ->shadowPolicy()
                    .runtimeKey());
}

TEST(RouteMatcherTest, Retry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2",
          "retry_policy": {
            "retry_on": "connect-failure"
          }
        },
        {
          "prefix": "/bar",
          "cluster": "www2"
        },
        {
          "prefix": "/",
          "cluster": "www2",
          "retry_policy": {
            "per_try_timeout_ms" : 1000,
            "num_retries": 3,
            "retry_on": "5xx,gateway-error,connect-failure"
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

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
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
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
                RetryPolicy::RETRY_ON_GATEWAY_ERROR,
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
}

TEST(RouteMatcherTest, GrpcRetry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2",
          "retry_policy": {
            "retry_on": "connect-failure"
          }
        },
        {
          "prefix": "/bar",
          "cluster": "www2"
        },
        {
          "prefix": "/",
          "cluster": "www2",
          "retry_policy": {
            "per_try_timeout_ms" : 1000,
            "num_retries": 3,
            "retry_on": "5xx,deadline-exceeded,resource-exhausted"
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

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
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
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

TEST(RouteMatcherTest, TestBadDefaultConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging"
        }
      ]
    }
  ],

  "internal_only_headers": [
    "x-lyft-user-id"
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, TestDuplicateDomainConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

// Test to detect if hostname matches are case-insensitive
TEST(RouteMatcherTest, TestCaseSensitiveDomainConfig) {
  std::string config_with_case_sensitive_domains = R"EOF(
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

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(
      ConfigImpl(parseRouteConfigurationFromV2Yaml(config_with_case_sensitive_domains), runtime, cm,
                 true),
      EnvoyException,
      "Only unique values for domains are permitted. Duplicate entry of domain www.lyft.com");
}

static Http::TestHeaderMapImpl genRedirectHeaders(const std::string& host, const std::string& path,
                                                  bool ssl, bool internal) {
  Http::TestHeaderMapImpl headers{
      {":authority", host}, {":path", path}, {"x-forwarded-proto", ssl ? "https" : "http"}};
  if (internal) {
    headers.addCopy("x-envoy-internal", "true");
  }

  return headers;
}

TEST(RouteMatcherTest, DirectResponse) {
  static const std::string v1_json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "require_ssl": "all",
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "api",
      "domains": ["api.lyft.com"],
      "require_ssl": "external_only",
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "redirect",
      "domains": ["redirect.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com"
        },
        {
          "prefix": "/bar",
          "path_redirect": "/new_bar"
        },
        {
          "prefix": "/baz",
          "host_redirect": "new.lyft.com",
          "path_redirect": "/new_baz"
        }
      ]
    }
  ]
}
  )EOF";

  const auto pathname =
      TestEnvironment::writeStringToFileForTest("direct_response_body", "Example text 3");

  // A superset of v1_json, with API v2 direct-response configuration added.
  static const std::string v2_yaml = R"EOF(
name: foo
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
      - match: { prefix: /foo }
        redirect: { host_redirect: new.lyft.com }
      - match: { prefix: /bar }
        redirect: { path_redirect: /new_bar }
      - match: { prefix: /baz }
        redirect: { host_redirect: new.lyft.com, path_redirect: /new_baz }
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

  auto testConfig = [](const ConfigImpl& config, bool test_v2 = false) {
    EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));
    {
      Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
      EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
    }
    {
      Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", false, false);
      EXPECT_EQ("https://www.lyft.com/foo",
                config.route(headers, 0)->directResponseEntry()->newPath(headers));
    }
    {
      Http::TestHeaderMapImpl headers = genRedirectHeaders("api.lyft.com", "/foo", false, true);
      EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
    }
    {
      Http::TestHeaderMapImpl headers = genRedirectHeaders("api.lyft.com", "/foo", false, false);
      EXPECT_EQ("https://api.lyft.com/foo",
                config.route(headers, 0)->directResponseEntry()->newPath(headers));
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
      EXPECT_EQ("http://new.lyft.com/foo",
                config.route(headers, 0)->directResponseEntry()->newPath(headers));
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("redirect.lyft.com", "/bar", true, false);
      EXPECT_EQ("https://redirect.lyft.com/new_bar",
                config.route(headers, 0)->directResponseEntry()->newPath(headers));
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("redirect.lyft.com", "/baz", true, false);
      EXPECT_EQ("https://new.lyft.com/new_baz",
                config.route(headers, 0)->directResponseEntry()->newPath(headers));
    }
    if (!test_v2) {
      return;
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("direct.example.com", "/gone", true, false);
      EXPECT_EQ(Http::Code::Gone, config.route(headers, 0)->directResponseEntry()->responseCode());
      EXPECT_EQ("Example text 1", config.route(headers, 0)->directResponseEntry()->responseBody());
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("direct.example.com", "/error", true, false);
      EXPECT_EQ(Http::Code::InternalServerError,
                config.route(headers, 0)->directResponseEntry()->responseCode());
      EXPECT_EQ("Example text 2", config.route(headers, 0)->directResponseEntry()->responseBody());
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("direct.example.com", "/no_body", true, false);
      EXPECT_EQ(Http::Code::OK, config.route(headers, 0)->directResponseEntry()->responseCode());
      EXPECT_TRUE(config.route(headers, 0)->directResponseEntry()->responseBody().empty());
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("direct.example.com", "/static", true, false);
      EXPECT_EQ(Http::Code::OK, config.route(headers, 0)->directResponseEntry()->responseCode());
      EXPECT_EQ("Example text 3", config.route(headers, 0)->directResponseEntry()->responseBody());
    }
    {
      Http::TestHeaderMapImpl headers =
          genRedirectHeaders("direct.example.com", "/other", true, false);
      EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
    }
  };

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  ConfigImpl v1_json_config(parseRouteConfigurationFromJson(v1_json), runtime, cm, true);
  testConfig(v1_json_config);

  ConfigImpl v2_yaml_config(parseRouteConfigurationFromV2Yaml(v2_yaml), runtime, cm, true);
  testConfig(v2_yaml_config, true);
}

TEST(RouteMatcherTest, ExclusiveRouteEntryOrDirectResponseEntry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "redirect",
      "domains": ["redirect.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
    EXPECT_EQ("www2", config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->routeEntry());
  }
}

TEST(RouteMatcherTest, ExclusiveWeightedClustersEntryOrDirectResponseEntry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
           "clusters" : [{ "name" : "www2", "weight" : 100 }]
          }
        }
      ]
    },
    {
      "name": "redirect",
      "domains": ["redirect.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
    EXPECT_EQ("www2", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->routeEntry());
  }
}

TEST(RouteMatcherTest, WeightedClusters) {
  std::string yaml = R"EOF(
virtual_hosts:
  - name: www1
    domains: ["www1.lyft.com"]
    routes:
      - match: { prefix: "/" }
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

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www1.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());
  }

  // Weighted Cluster with no runtime, default total weight
  {
    Http::TestHeaderMapImpl headers = genHeaders("www1.lyft.com", "/foo", "GET");
    EXPECT_EQ("cluster1", config.route(headers, 115)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 445)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 560)->routeEntry()->clusterName());
  }

  // Make sure weighted cluster entries call through to the parent when needed.
  {
    Http::TestHeaderMapImpl headers = genHeaders("www1.lyft.com", "/foo", "GET");
    const RouteEntry* route = config.route(headers, 115)->routeEntry();
    EXPECT_EQ(nullptr, route->hashPolicy());
    EXPECT_TRUE(route->opaqueConfig().empty());
    EXPECT_FALSE(route->autoHostRewrite());
    EXPECT_FALSE(route->useWebSocket());
    EXPECT_TRUE(route->includeVirtualHostRateLimits());
  }

  // Weighted Cluster with no runtime, total weight = 10000
  {
    Http::TestHeaderMapImpl headers = genHeaders("www2.lyft.com", "/foo", "GET");
    EXPECT_EQ("cluster1", config.route(headers, 1150)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 4500)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 8900)->routeEntry()->clusterName());
  }

  // Weighted Cluster with valid runtime values, default total weight
  {
    Http::TestHeaderMapImpl headers = genHeaders("www3.lyft.com", "/foo", "GET");
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
    Http::TestHeaderMapImpl headers = genHeaders("www3.lyft.com", "/foo", "GET");
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
    Http::TestHeaderMapImpl headers = genHeaders("www4.lyft.com", "/foo", "GET");
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
    Http::TestHeaderMapImpl headers = genHeaders("www4.lyft.com", "/foo", "GET");
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

TEST(RouteMatcherTest, ExclusiveWeightedClustersOrClusterConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 30 },
              { "name" : "cluster2", "weight" : 30 },
              { "name" : "cluster3", "weight" : 40 }
            ]
          },
          "cluster" : "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, WeightedClustersMissingClusterList) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "runtime_key_prefix" : "www2"
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, WeightedClustersEmptyClustersList) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "runtime_key_prefix" : "www2",
            "clusters" : []
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, WeightedClustersSumOFWeightsNotEqualToMax) {
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

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true),
                            EnvoyException,
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

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true),
                            EnvoyException,
                            "Sum of weights in the weighted_cluster should add up to 99");
}

TEST(RouteMatcherTest, TestWeightedClusterWithMissingWeights) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 50 },
              { "name" : "cluster2", "weight" : 50 },
              { "name" : "cluster3"}
            ]
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, TestWeightedClusterInvalidClusterName) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 33 },
              { "name" : "cluster2", "weight" : 33 },
              { "name" : "cluster3-invalid", "weight": 34}
            ]
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("cluster1")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  EXPECT_CALL(cm, get("cluster2")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  EXPECT_CALL(cm, get("cluster3-invalid")).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(NullConfigImplTest, All) {
  NullConfigImpl config;
  Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/baz", true, false);
  EXPECT_EQ(nullptr, config.route(headers, 0));
  EXPECT_EQ(0UL, config.internalOnlyHeaders().size());
}

TEST(BadHttpRouteConfigurationsTest, BadRouteConfig) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2"
          }
        ]
      }
    ],
    "fake_entry" : "fake_type"
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(BadHttpRouteConfigurationsTest, BadVirtualHostConfig) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "router" : {
          "cluster" : "my_cluster"
        },
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfig) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2",
            "timeout_ms" : "1234"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndPath) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "path": "/foo",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndRegex) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "regex": "/[bc]at",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPathAndRegex) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "path": "/foo",
            "regex": "/[bc]at",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
  ;
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndPathAndRegex) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "path": "/foo",
            "regex": "/[bc]at",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigMissingPathSpecifier) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigNoRedirectNoClusters) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/api"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(
      ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true), EnvoyException,
      "routes must have redirect or one of cluster/cluster_header/weighted_clusters")
}

TEST(RouteMatcherTest, TestOpaqueConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats",
          "opaque_config" : {
              "name1": "value1",
              "name2": "value2"
          }
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  const std::multimap<std::string, std::string>& opaque_config =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->opaqueConfig();

  EXPECT_EQ(opaque_config.find("name1")->second, "value1");
  EXPECT_EQ(opaque_config.find("name2")->second, "value2");
}

TEST(RoutePropertyTest, excludeVHRateLimits) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
  std::unique_ptr<ConfigImpl> config_ptr;

  config_ptr.reset(new ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true));
  EXPECT_TRUE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());

  json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2",
            "rate_limits": [
              {
                "actions": [
                  {
                    "type": "remote_address"
                  }
                ]
              }
            ]
          }
        ]
      }
    ]
  }
  )EOF";

  config_ptr.reset(new ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true));
  EXPECT_FALSE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());

  json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2",
            "include_vh_rate_limits": true,
            "rate_limits": [
              {
                "actions": [
                  {
                    "type": "remote_address"
                  }
                ]
              }
            ]
          }
        ]
      }
    ]
  }
  )EOF";

  config_ptr.reset(new ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true));
  EXPECT_TRUE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());
}

TEST(RoutePropertyTest, TestVHostCorsConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "cors" : {
        "allow_origin": ["test-origin"],
        "allow_methods": "test-methods",
        "allow_headers": "test-headers",
        "expose_headers": "test-expose-headers",
        "max_age": "test-max-age",
        "allow_credentials": true
      },
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats"
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)
          ->routeEntry()
          ->virtualHost()
          .corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), true);
  EXPECT_THAT(cors_policy->allowOrigins(), ElementsAreArray({"test-origin"}));
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

TEST(RoutePropertyTest, TestRouteCorsConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats",
          "cors" : {
              "allow_origin": ["test-origin"],
              "allow_methods": "test-methods",
              "allow_headers": "test-headers",
              "expose_headers": "test-expose-headers",
              "max_age": "test-max-age",
              "allow_credentials": true
          }
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), true);
  EXPECT_THAT(cors_policy->allowOrigins(), ElementsAreArray({"test-origin"}));
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

TEST(RoutePropertyTest, TestBadCorsConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats",
          "cors" : {
              "enabled": "true",
              "allow_credentials": "true"
          }
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouterMatcherTest, Decorator) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "foo",
          "decorator": {
            "operation": "myFoo"
          }
        },
        {
          "prefix": "/bar",
          "cluster": "bar"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    Tracing::MockSpan span;
    EXPECT_CALL(span, setOperation("myFoo"));
    route->decorator()->apply(span);
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ(nullptr, route->decorator());
  }
}

TEST(CustomRequestHeadersTest, AddNewHeader) {
  const std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": [
          "lyft.com",
          "www.lyft.com",
          "w.lyft.com",
          "ww.lyft.com",
          "wwww.lyft.com"
        ],
        "request_headers_to_add": [
          {
            "key": "x-client-ip",
            "value": "%CLIENT_IP%"
          }
        ],
        "routes": [
          {
            "prefix": "/new_endpoint",
            "prefix_rewrite": "/api/new_endpoint",
            "cluster": "www2",
            "request_headers_to_add": [
              {
                "key": "x-client-ip",
                "value": "%CLIENT_IP%"
              }
            ]
          }
        ]
      }
    ],
    "request_headers_to_add": [
      {
        "key": "x-client-ip",
        "value": "%CLIENT_IP%"
      }
    ]
  }
  )EOF";
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);
  Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
  const RouteEntry* route = config.route(headers, 0)->routeEntry();
  route->finalizeRequestHeaders(headers, request_info);
  EXPECT_EQ("127.0.0.1", headers.get_("x-client-ip"));
}

TEST(CustomRequestHeadersTest, CustomHeaderWrongFormat) {
  const std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": [
          "lyft.com",
          "www.lyft.com",
          "w.lyft.com",
          "ww.lyft.com",
          "wwww.lyft.com"
        ],
        "request_headers_to_add": [
          {
            "key": "x-client-ip",
            "value": "%CLIENT_IP%"
          }
        ],
        "routes": [
          {
            "prefix": "/new_endpoint",
            "prefix_rewrite": "/api/new_endpoint",
            "cluster": "www2",
            "request_headers_to_add": [
              {
                "key": "x-client-ip",
                "value": "%CLIENT_IP"
              }
            ]
          }
        ]
      }
    ],
    "request_headers_to_add": [
      {
        "key": "x-client-ip",
        "value": "%CLIENT_IP"
      }
    ]
  }
  )EOF";
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  EXPECT_THROW_WITH_MESSAGE(
      ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true), EnvoyException,
      "Invalid header configuration. Un-terminated variable expression 'CLIENT_IP'");
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

  MetadataMatchCriteriaImplConstPtr matches = parent_matches.mergeMatchCriteria(metadata_struct);

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

TEST(RouteEntryMetadataMatchTest, ParsesMetadata) {
  auto route_config = envoy::api::v2::RouteConfiguration();
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

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(route_config, runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/both", true, true);
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
    Http::TestHeaderMapImpl headers =
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
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/route-only", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->directResponseEntry());

    auto* route_entry = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www3", route_entry->clusterName());
    auto* matches = route_entry->metadataMatchCriteria();
    EXPECT_NE(matches, nullptr);
    EXPECT_EQ(matches->metadataMatchCriteria().size(), 1);
    EXPECT_EQ(matches->metadataMatchCriteria().at(0)->name(), "r3_key");
  }

  {
    Http::TestHeaderMapImpl headers =
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

TEST(ConfigUtility, ParseResponseCode) {
  const std::vector<
      std::pair<envoy::api::v2::route::RedirectAction::RedirectResponseCode, Http::Code>>
      test_set = {
          std::make_pair(envoy::api::v2::route::RedirectAction::MOVED_PERMANENTLY,
                         Http::Code::MovedPermanently),
          std::make_pair(envoy::api::v2::route::RedirectAction::FOUND, Http::Code::Found),
          std::make_pair(envoy::api::v2::route::RedirectAction::SEE_OTHER, Http::Code::SeeOther),
          std::make_pair(envoy::api::v2::route::RedirectAction::TEMPORARY_REDIRECT,
                         Http::Code::TemporaryRedirect),
          std::make_pair(envoy::api::v2::route::RedirectAction::PERMANENT_REDIRECT,
                         Http::Code::PermanentRedirect)};
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, ConfigUtility::parseRedirectResponseCode(test_case.first));
  }
}

TEST(RouteConfigurationV2, RedirectCode) {
  std::string yaml = R"EOF(
name: foo
virtual_hosts:
  - name: redirect
    domains: [redirect.lyft.com]
    routes:
      - match: { prefix: "/"}
        redirect: { host_redirect: new.lyft.com, response_code: TEMPORARY_REDIRECT }

  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true);

  EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->directResponseEntry()->newPath(headers));
    EXPECT_EQ(Http::Code::TemporaryRedirect,
              config.route(headers, 0)->directResponseEntry()->responseCode());
  }
}

// Test the parsing of direct response configurations within routes.
TEST(RouteConfigurationV2, DirectResponse) {
  std::string yaml = R"EOF(
name: foo
virtual_hosts:
  - name: direct
    domains: [example.com]
    routes:
      - match: { prefix: "/"}
        direct_response: { status: 200, body: { inline_string: "content" } }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true);

  const auto* direct_response =
      config.route(genHeaders("example.com", "/", "GET"), 0)->directResponseEntry();
  EXPECT_NE(nullptr, direct_response);
  EXPECT_EQ(Http::Code::OK, direct_response->responseCode());
  EXPECT_STREQ("content", direct_response->responseBody().c_str());
}

// Test the parsing of a direct response configuration where the response body is too large.
TEST(RouteConfigurationV2, DirectResponseTooLarge) {
  std::string response_body(4097, 'A');
  std::string yaml = R"EOF(
name: foo
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

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW_WITH_MESSAGE(
      ConfigImpl invalid_config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true),
      EnvoyException, "response body size is 4097 bytes; maximum is 4096");
}

TEST(RouteConfigurationV2, Metadata) {
  std::string yaml = R"EOF(
name: foo
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/"}
        route: { cluster: www2 }
        metadata: { filter_metadata: { com.bar.foo: { baz: test_value } } }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromV2Yaml(yaml), runtime, cm, true);

  auto* route_entry = config.route(genHeaders("www.foo.com", "/", "GET"), 0)->routeEntry();

  const auto& metadata = route_entry->metadata();

  EXPECT_EQ("test_value",
            Envoy::Config::Metadata::metadataValue(metadata, "com.bar.foo", "baz").string_value());
}

} // namespace
} // namespace Router
} // namespace Envoy
