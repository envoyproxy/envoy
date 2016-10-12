#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/router/config_impl.h"

#include "test/mocks/upstream/mocks.h"
#include "test/mocks/runtime/mocks.h"

using testing::_;
using testing::ContainerEq;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrNe;

namespace Router {

static Http::HeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                      const std::string& method) {
  return Http::HeaderMapImpl{{":authority", host}, {":path", path}, {":method", method}};
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
  ]
}
  )EOF";

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  // Base routing testing.
  EXPECT_EQ("instant-server",
            config.routeForRequest(genHeaders("api.lyft.com", "/", "GET"), 0)->clusterName());
  EXPECT_EQ("ats", config.routeForRequest(genHeaders("api.lyft.com", "/api/leads/me", "GET"), 0)
                       ->clusterName());
  EXPECT_EQ("ats",
            config.routeForRequest(genHeaders("api.lyft.com", "/api/application_data", "GET"), 0)
                ->clusterName());

  EXPECT_EQ("locations",
            config.routeForRequest(genHeaders("api.lyft.com", "/api/locations?works=true", "GET"),
                                   0)->clusterName());
  EXPECT_EQ("locations", config.routeForRequest(genHeaders("api.lyft.com", "/api/locations", "GET"),
                                                0)->clusterName());
  EXPECT_EQ("www2",
            config.routeForRequest(genHeaders("lyft.com", "/foo", "GET"), 0)->clusterName());
  EXPECT_EQ("root_www2",
            config.routeForRequest(genHeaders("wwww.lyft.com", "/", "GET"), 0)->clusterName());

  // Timeout testing.
  EXPECT_EQ(std::chrono::milliseconds(30000),
            config.routeForRequest(genHeaders("api.lyft.com", "/", "GET"), 0)->timeout());
  EXPECT_EQ(
      std::chrono::milliseconds(15000),
      config.routeForRequest(genHeaders("api.lyft.com", "/api/leads/me", "GET"), 0)->timeout());

  // Prefix rewrite testing.
  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", route->virtualHostName());
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/api/new_endpoint/foo", headers.get(Http::Headers::get().Path));
  }

  // Prefix rewrite on path match with query string params
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/api/locations?works=true", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/rewrote?works=true", headers.get(Http::Headers::get().Path));
  }

  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/foo", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/bar", headers.get(Http::Headers::get().Path));
  }

  // Host rewrite testing.
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/host/rewrite/me", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("new_host", headers.get(Http::Headers::get().Host));
  }

  // Case sensitive rewrite matching test.
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/API/locations?works=true", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/rewrote?works=true", headers.get(Http::Headers::get().Path));
  }

  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/fooD", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/cAndy", headers.get(Http::Headers::get().Path));
  }

  // Case sensitive is set to true and will not rewrite
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/FOO", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/FOO", headers.get(Http::Headers::get().Path));
  }

  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/ApPles", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/ApPles", headers.get(Http::Headers::get().Path));
  }

  // Case insensitive set to false so there is no rewrite
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/oLDhost/rewrite/me", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("api.lyft.com", headers.get(Http::Headers::get().Host));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/Tart", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("/Tart", headers.get(Http::Headers::get().Path));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/newhost/rewrite/me", "GET");
    const RouteEntry* route = config.routeForRequest(headers, 0);
    route->finalizeRequestHeaders(headers);
    EXPECT_EQ("new_host", headers.get(Http::Headers::get().Host));
  }

  // Header manipulaton testing.
  EXPECT_THAT(std::list<Http::LowerCaseString>({"x-lyft-user-id"}),
              ContainerEq(config.internalOnlyHeaders()));
  EXPECT_THAT((std::list<std::pair<Http::LowerCaseString, std::string>>(
                  {{"x-envoy-upstream-canary", "true"}})),
              ContainerEq(config.responseHeadersToAdd()));
  EXPECT_THAT(
      std::list<Http::LowerCaseString>({"x-envoy-upstream-canary", "x-envoy-virtual-cluster"}),
      ContainerEq(config.responseHeadersToRemove()));

  // Virtual cluster testing.
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "GET");
    EXPECT_EQ("other", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/blah", "POST");
    EXPECT_EQ("other", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "POST");
    EXPECT_EQ("ride_request", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123", "PUT");
    EXPECT_EQ("update_ride", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123/chargeaccounts", "POST");
    EXPECT_EQ("cc_add", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/hello123", "PUT");
    EXPECT_EQ("cc_add", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/validate", "PUT");
    EXPECT_EQ("other", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/foo/bar", "PUT");
    EXPECT_EQ("other", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/users", "POST");
    EXPECT_EQ("create_user_login",
              config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123", "PUT");
    EXPECT_EQ("update_user", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123/location", "POST");
    EXPECT_EQ("ulu", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
  {
    Http::HeaderMapImpl headers = genHeaders("api.lyft.com", "/something/else", "GET");
    EXPECT_EQ("other", config.routeForRequest(headers, 0)->virtualCluster(headers)->name());
  }
}

TEST(RouteMatcherTest, InvalidPriority) {
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
          "priority": "foo"
        }
      ]
    }
  ]
}
  )EOF";

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(loader, runtime, cm), EnvoyException);
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
        {"pattern": "^/bar$", "method": "POST", "name": "foo", "priority": "high"}]
    }
  ]
}
  )EOF";

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  EXPECT_EQ(Upstream::ResourcePriority::High,
            config.routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)->priority());
  EXPECT_EQ(Upstream::ResourcePriority::Default,
            config.routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)->priority());

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "POST");
    EXPECT_EQ(Upstream::ResourcePriority::High,
              config.routeForRequest(headers, 0)->virtualCluster(headers)->priority());
  }

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    EXPECT_EQ(Upstream::ResourcePriority::Default,
              config.routeForRequest(headers, 0)->virtualCluster(headers)->priority());
  }
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
          "cluster": "local_service_without_headers"
        }
      ]
    }
  ]
}
  )EOF";

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  {
    EXPECT_EQ("local_service_without_headers",
              config.routeForRequest(genHeaders("www.lyft.com", "/", "GET"), 0)->clusterName());
  }

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addViaCopy("test_header", "test");
    EXPECT_EQ("local_service_with_headers", config.routeForRequest(headers, 0)->clusterName());
  }

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addViaCopy("test_header_multiple1", "test1");
    headers.addViaCopy("test_header_multiple2", "test2");
    EXPECT_EQ("local_service_with_multiple_headers",
              config.routeForRequest(headers, 0)->clusterName());
  }

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addViaCopy("non_existent_header", "foo");
    EXPECT_EQ("local_service_without_headers", config.routeForRequest(headers, 0)->clusterName());
  }

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addViaCopy("test_header_presence", "test");
    EXPECT_EQ("local_service_with_empty_headers",
              config.routeForRequest(headers, 0)->clusterName());
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

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  {
    EXPECT_EQ("local_service",
              config.routeForRequest(genHeaders("www.lyft.com", "/", "GET"), 0)->clusterName());
  }

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addViaCopy("content-type", "application/grpc");
    EXPECT_EQ("local_service_grpc", config.routeForRequest(headers, 0)->clusterName());
  }

  {
    Http::HeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addViaCopy("content-type", "foo");
    EXPECT_EQ("local_service", config.routeForRequest(headers, 0)->clusterName());
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

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  Runtime::MockSnapshot snapshot;

  ON_CALL(runtime, snapshot()).WillByDefault(ReturnRef(snapshot));

  ConfigImpl config(loader, runtime, cm);

  EXPECT_CALL(snapshot, featureEnabled("some_key", 50, 10)).WillOnce(Return(true));
  EXPECT_EQ("something_else",
            config.routeForRequest(genHeaders("www.lyft.com", "/", "GET"), 10)->clusterName());

  EXPECT_CALL(snapshot, featureEnabled("some_key", 50, 20)).WillOnce(Return(false));
  EXPECT_EQ("www2",
            config.routeForRequest(genHeaders("www.lyft.com", "/", "GET"), 20)->clusterName());
}

TEST(RouteMatcherTest, RateLimit) {
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
          "rate_limit": {
            "global": true
          }
        },
        {
          "prefix": "/bar",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  EXPECT_TRUE(config.routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                  ->rateLimitPolicy()
                  .doGlobalLimiting());
  EXPECT_FALSE(config.routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                   ->rateLimitPolicy()
                   .doGlobalLimiting());
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

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(&cm.cluster_));
  EXPECT_CALL(cm, get("some_cluster")).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(ConfigImpl(loader, runtime, cm), EnvoyException);
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

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  EXPECT_EQ("some_cluster", config.routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                                ->shadowPolicy()
                                .cluster());
  EXPECT_EQ("", config.routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->shadowPolicy()
                    .runtimeKey());

  EXPECT_EQ("some_cluster2", config.routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                                 ->shadowPolicy()
                                 .cluster());
  EXPECT_EQ("foo", config.routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                       ->shadowPolicy()
                       .runtimeKey());

  EXPECT_EQ("", config.routeForRequest(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                    ->shadowPolicy()
                    .cluster());
  EXPECT_EQ("", config.routeForRequest(genHeaders("www.lyft.com", "/baz", "GET"), 0)
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
            "num_retries": 3,
            "retry_on": "5xx,connect-failure"
          }
        }
      ]
    }
  ]
}
  )EOF";

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  EXPECT_EQ(1U, config.routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE,
            config.routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->retryPolicy()
                .retryOn());

  EXPECT_EQ(0U, config.routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(0U, config.routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->retryPolicy()
                    .retryOn());

  EXPECT_EQ(3U, config.routeForRequest(genHeaders("www.lyft.com", "/", "GET"), 0)
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(
      RetryPolicy::RETRY_ON_CONNECT_FAILURE | RetryPolicy::RETRY_ON_5XX,
      config.routeForRequest(genHeaders("www.lyft.com", "/", "GET"), 0)->retryPolicy().retryOn());
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

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl config(loader, runtime, cm), EnvoyException);
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

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl config(loader, runtime, cm), EnvoyException);
}

static Http::HeaderMapImpl genRedirectHeaders(const std::string& host, const std::string& path,
                                              bool ssl, bool internal) {
  Http::HeaderMapImpl headers{
      {":authority", host}, {":path", path}, {"x-forwarded-proto", ssl ? "https" : "http"}};
  if (internal) {
    headers.addViaCopy("x-envoy-internal", "true");
  }

  return headers;
}

TEST(RouteMatcherTest, Redirect) {
  std::string json = R"EOF(
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

  Json::StringLoader loader(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(loader, runtime, cm);

  EXPECT_EQ(nullptr,
            config.redirectRequest(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));
  EXPECT_EQ(nullptr,
            config.routeForRequest(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));

  {
    Http::HeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.redirectRequest(headers, 0));
  }
  {
    Http::HeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", false, false);
    EXPECT_EQ("https://www.lyft.com/foo", config.redirectRequest(headers, 0)->newPath(headers));
  }
  {
    Http::HeaderMapImpl headers = genRedirectHeaders("api.lyft.com", "/foo", false, true);
    EXPECT_EQ(nullptr, config.redirectRequest(headers, 0));
  }
  {
    Http::HeaderMapImpl headers = genRedirectHeaders("api.lyft.com", "/foo", false, false);
    EXPECT_EQ("https://api.lyft.com/foo", config.redirectRequest(headers, 0)->newPath(headers));
  }
  {
    Http::HeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo", config.redirectRequest(headers, 0)->newPath(headers));
  }
  {
    Http::HeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/bar", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new_bar",
              config.redirectRequest(headers, 0)->newPath(headers));
  }
  {
    Http::HeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/baz", true, false);
    EXPECT_EQ("https://new.lyft.com/new_baz", config.redirectRequest(headers, 0)->newPath(headers));
  }
}

TEST(NullConfigImplTest, All) {
  NullConfigImpl config;
  Http::HeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/baz", true, false);
  EXPECT_EQ(nullptr, config.redirectRequest(headers, 0));
  EXPECT_EQ(nullptr, config.routeForRequest(headers, 0));
  EXPECT_EQ(0UL, config.internalOnlyHeaders().size());
  EXPECT_EQ(0UL, config.responseHeadersToAdd().size());
  EXPECT_EQ(0UL, config.responseHeadersToRemove().size());
}

} // Router
