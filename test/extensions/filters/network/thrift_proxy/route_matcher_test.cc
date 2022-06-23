#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.h"
#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.validate.h"

#include "source/common/config/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/router/config.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_impl.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {
namespace {

class ThriftRouteMatcherTest : public testing::Test {
public:
  ThriftRouteMatcherTest() : engine_(std::make_unique<Regex::GoogleReEngine>()) {}

protected:
  RouteMatcher createMatcher(
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& route) {
    return RouteMatcher(route, absl::nullopt);
  }

  RouteMatcher createMatcher(const std::string& yaml) {
    auto config = parseRouteConfigurationFromV3Yaml(yaml);
    return createMatcher(config);
  }

private:
  ScopedInjectableLoader<Regex::Engine> engine_;

  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration
  parseRouteConfigurationFromV3Yaml(const std::string& yaml) {
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration route_config;
    TestUtility::loadFromYaml(yaml, route_config);
    TestUtility::validate(route_config);
    return route_config;
  }
};

TEST_F(ThriftRouteMatcherTest, RouteByMethodNameWithNoInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      cluster: "cluster1"
  - match:
      method_name: "method2"
    route:
      cluster: "cluster2"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  metadata.setMethodName("unknown");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  metadata.setMethodName("METHOD1");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  metadata.setMethodName("method1");
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());

  metadata.setMethodName("method2");
  RouteConstSharedPtr route2 = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route2);
  EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteByMethodNameWithInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      cluster: "cluster1"
  - match:
      method_name: "method2"
      invert: true
    route:
      cluster: "cluster2"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster2", route->routeEntry()->clusterName());

  metadata.setMethodName("unknown");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster2", route->routeEntry()->clusterName());

  metadata.setMethodName("METHOD1");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster2", route->routeEntry()->clusterName());

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());

  metadata.setMethodName("method2");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);
}

TEST_F(ThriftRouteMatcherTest, RouteByAnyMethodNameWithNoInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      cluster: "cluster1"
  - match:
      method_name: ""
    route:
      cluster: "cluster2"
)EOF";

  auto matcher = createMatcher(yaml);

  {
    MessageMetadata metadata;
    metadata.setMethodName("method1");
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_EQ("cluster1", route->routeEntry()->clusterName());

    metadata.setMethodName("anything");
    RouteConstSharedPtr route2 = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route2);
    EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
  }

  {
    MessageMetadata metadata;
    RouteConstSharedPtr route2 = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route2);
    EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
  }
}

TEST_F(ThriftRouteMatcherTest, RouteByAnyMethodNameWithInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: ""
      invert: true
    route:
      cluster: "cluster2"
)EOF";

  EXPECT_THROW(createMatcher(yaml), EnvoyException);
}

TEST_F(ThriftRouteMatcherTest, RouteByServiceNameWithNoInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      cluster: "cluster1"
  - match:
      service_name: "service2"
    route:
      cluster: "cluster2"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  metadata.setMethodName("unknown");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  metadata.setMethodName("METHOD1");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  metadata.setMethodName("service2:method1");
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster2", route->routeEntry()->clusterName());

  metadata.setMethodName("service2:method2");
  RouteConstSharedPtr route2 = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route2);
  EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteByServiceNameWithInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      cluster: "cluster1"
  - match:
      service_name: "service2"
      invert: true
    route:
      cluster: "cluster2"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster2", route->routeEntry()->clusterName());

  metadata.setMethodName("unknown");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster2", route->routeEntry()->clusterName());

  metadata.setMethodName("METHOD1");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster2", route->routeEntry()->clusterName());

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());

  metadata.setMethodName("service2:method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);
}

TEST_F(ThriftRouteMatcherTest, RouteByAnyServiceNameWithNoInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      cluster: "cluster1"
  - match:
      service_name: ""
    route:
      cluster: "cluster2"
)EOF";

  auto matcher = createMatcher(yaml);

  {
    MessageMetadata metadata;
    metadata.setMethodName("method1");
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_EQ("cluster1", route->routeEntry()->clusterName());

    metadata.setMethodName("anything");
    RouteConstSharedPtr route2 = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route2);
    EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
  }

  {
    MessageMetadata metadata;
    RouteConstSharedPtr route2 = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route2);
    EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
  }
}

TEST_F(ThriftRouteMatcherTest, RouteByAnyServiceNameWithInversion) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      service_name: ""
      invert: true
    route:
      cluster: "cluster2"
)EOF";

  EXPECT_THROW(createMatcher(yaml), EnvoyException);
}

TEST_F(ThriftRouteMatcherTest, RouteByExactHeaderMatcher) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
      headers:
      - name: "x-header-1"
        string_match:
          exact: "x-value-1"
    route:
      cluster: "cluster1"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-header-1"), "x-value-1");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteByRegexHeaderMatcher) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
      headers:
      - name: "x-version"
        string_match:
          safe_regex:
            regex: "0.[5-9]"
    route:
      cluster: "cluster1"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-version"), "0.1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);
  metadata.requestHeaders().remove(Http::LowerCaseString("x-version"));

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-version"), "0.8");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteByRangeHeaderMatcher) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
      headers:
      - name: "x-user-id"
        range_match:
          start: 100
          end: 200
    route:
      cluster: "cluster1"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-user-id"), "50");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);
  metadata.requestHeaders().remove(Http::LowerCaseString("x-user-id"));

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-user-id"), "199");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteByPresentHeaderMatcher) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
      headers:
      - name: "x-user-id"
        present_match: true
    route:
      cluster: "cluster1"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-user-id"), "50");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
  metadata.requestHeaders().remove(Http::LowerCaseString("x-user-id"));

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-user-id"), "");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteByPrefixHeaderMatcher) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
      headers:
      - name: "x-header-1"
        string_match:
          prefix: "user_id:"
    route:
      cluster: "cluster1"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-header-1"), "500");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);
  metadata.requestHeaders().remove(Http::LowerCaseString("x-header-1"));

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-header-1"), "user_id:500");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteBySuffixHeaderMatcher) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
      headers:
      - name: "x-header-1"
        string_match:
          suffix: "asdf"
    route:
      cluster: "cluster1"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-header-1"), "asdfvalue");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);
  metadata.requestHeaders().remove(Http::LowerCaseString("x-header-1"));

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-header-1"), "valueasdfvalue");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);
  metadata.requestHeaders().remove(Http::LowerCaseString("x-header-1"));

  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-header-1"), "value:asdf");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, RouteByClusterHeader) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: ""
    route:
      cluster_header: "x-cluster"
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  RouteConstSharedPtr route;

  // No method nor header.
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  // Method, but no header.
  metadata.setMethodName("method1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  // The wrong header is present.
  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-something"), "cluster1");
  route = matcher.route(metadata, 0);
  EXPECT_EQ(nullptr, route);

  // Header is present.
  metadata.requestHeaders().addCopy(Http::LowerCaseString("x-cluster"), "cluster1");
  route = matcher.route(metadata, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());
}

TEST_F(ThriftRouteMatcherTest, WeightedClusters) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 30
          - name: cluster2
            weight: 30
          - name: cluster3
            weight: 40
  - match:
      method_name: "method2"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 2000
          - name: cluster2
            weight: 3000
          - name: cluster3
            weight: 5000
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;

  {
    metadata.setMethodName("method1");
    EXPECT_EQ("cluster1", matcher.route(metadata, 0)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 29)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 30)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 59)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 60)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 99)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 100)->routeEntry()->clusterName());
  }

  {
    metadata.setMethodName("method2");
    EXPECT_EQ("cluster1", matcher.route(metadata, 0)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 1999)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 2000)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 4999)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 5000)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 9999)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 10000)->routeEntry()->clusterName());
  }
}

TEST_F(ThriftRouteMatcherTest, WeightedClusterMissingWeight) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method2"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 20000
          - name: cluster2
          - name: cluster3
            weight: 5000
)EOF";

  EXPECT_THROW(createMatcher(yaml), EnvoyException);
}

TEST_F(ThriftRouteMatcherTest, RouteActionMetadataMatch) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      cluster: cluster1
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
            k2: v2
  - match:
      method_name: "method2"
    route:
      cluster: cluster2
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;

  // match with metadata
  {
    metadata.setMethodName("method1");
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    ProtobufWkt::Value v1, v2;
    v1.set_string_value("v1");
    v2.set_string_value("v2");
    HashedValue hv1(v1), hv2(v2);

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());
  }

  // match with no metadata
  {
    metadata.setMethodName("method2");
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    EXPECT_EQ(nullptr, route->routeEntry()->metadataMatchCriteria());
  }
}

TEST_F(ThriftRouteMatcherTest, WeightedClusterMetadataMatch) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 2000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k1: v1
                  k2: v2
          - name: cluster2
            weight: 3000
            metadata_match:
              filter_metadata:
                not.envoy.lb:
                  k1: v1
                  k2: v2
          - name: cluster3
            weight: 5000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k3: v3
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  metadata.setMethodName("method1");
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  v3.set_string_value("v3");
  HashedValue hv1(v1), hv2(v2), hv3(v3);

  // match with multiple sets of weighted cluster metadata criteria defined
  {
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());

    EXPECT_EQ(Http::LowerCaseString{""}, route->routeEntry()->clusterHeader());
  }

  // match with weighted cluster with different metadata key
  {
    RouteConstSharedPtr route = matcher.route(metadata, 2001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    EXPECT_EQ(nullptr, route->routeEntry()->metadataMatchCriteria());
  }

  // weighted cluster match with single metadata entry
  {
    RouteConstSharedPtr route = matcher.route(metadata, 5001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(1, mmc.size());

    EXPECT_EQ("k3", mmc[0]->name());
    EXPECT_EQ(hv3, mmc[0]->value());
  }
}

TEST_F(ThriftRouteMatcherTest, WeightedClusterRouteActionMetadataMatchMerged) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method_name: "method1"
    route:
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
            k2: v2
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 2000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k3: v3
          - name: cluster2
            weight: 3000
          - name: cluster3
            weight: 5000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k2: v3
)EOF";

  auto matcher = createMatcher(yaml);
  MessageMetadata metadata;
  metadata.setMethodName("method1");
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  v3.set_string_value("v3");
  HashedValue hv1(v1), hv2(v2), hv3(v3);

  // match with weighted cluster metadata and route action metadata
  {
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(3, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());

    EXPECT_EQ("k3", mmc[2]->name());
    EXPECT_EQ(hv3, mmc[2]->value());
  }

  // match with just route action metadata
  {
    RouteConstSharedPtr route = matcher.route(metadata, 2001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());
  }

  // match with weighted cluster metadata and route action metadata merged
  {
    RouteConstSharedPtr route = matcher.route(metadata, 5001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv3, mmc[1]->value());
  }
}

// Test that the route entry has metadata match criteria when using a cluster header.
TEST_F(ThriftRouteMatcherTest, ClusterHeaderMetadataMatch) {
  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration config;
  {
    config.set_name("config");
    auto* route = config.add_routes();
    route->mutable_match()->set_method_name("method1");
    auto* action = route->mutable_route();
    action->set_cluster_header("header_name");
    auto* metadata = action->mutable_metadata_match();
    Envoy::Config::Metadata::mutableMetadataValue(*metadata, "envoy.lb", "k1")
        .set_string_value("v1");
    Envoy::Config::Metadata::mutableMetadataValue(*metadata, "envoy.lb", "k2")
        .set_string_value("v2");

    auto* route2 = config.add_routes();
    route2->mutable_match()->set_method_name("method2");
    auto* action2 = route2->mutable_route();
    action2->set_cluster("cluster2");
  }

  auto matcher = createMatcher(config);

  // match with metadata
  {
    MessageMetadata metadata;
    metadata.setMethodName("method1");
    metadata.requestHeaders().addCopy(Http::LowerCaseString{"header_name"}, "cluster1");
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    EXPECT_EQ(Http::LowerCaseString{"header_name"}, route->routeEntry()->clusterHeader());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    ProtobufWkt::Value v1, v2;
    v1.set_string_value("v1");
    v2.set_string_value("v2");
    HashedValue hv1(v1), hv2(v2);

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());
  }

  // match with no metadata
  {
    MessageMetadata metadata;
    metadata.setMethodName("method2");
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    EXPECT_EQ(nullptr, route->routeEntry()->metadataMatchCriteria());

    EXPECT_EQ(Http::LowerCaseString{""}, route->routeEntry()->clusterHeader());
  }
}

// Tests that weighted cluster route entries can be configured to strip the service name.
TEST_F(ThriftRouteMatcherTest, WeightedClusterWithStripServiceEnabled) {
  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration config;
  {
    config.set_name("config");
    auto* route = config.add_routes();
    route->mutable_match()->set_method_name("method1");
    auto* action = route->mutable_route();
    auto* cluster1 = action->mutable_weighted_clusters()->add_clusters();
    cluster1->set_name("cluster1");
    cluster1->mutable_weight()->set_value(50);
    auto* cluster2 = action->mutable_weighted_clusters()->add_clusters();
    cluster2->set_name("cluster2");
    cluster2->mutable_weight()->set_value(50);
    action->set_strip_service_name(true);
  }

  auto matcher = createMatcher(config);

  MessageMetadata metadata;
  metadata.setMethodName("method1");

  EXPECT_TRUE(matcher.route(metadata, 0)->routeEntry()->stripServiceName());
}

// Tests that dynamic route entries can be configured to strip the service name.
TEST_F(ThriftRouteMatcherTest, ClusterHeaderWithStripServiceEnabled) {
  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration config;
  {
    config.set_name("config");
    auto* route = config.add_routes();
    route->mutable_match()->set_method_name("method1");
    auto* action = route->mutable_route();
    action->set_cluster_header("header_name");
    action->set_strip_service_name(true);
  }

  auto matcher = createMatcher(config);

  MessageMetadata metadata;
  metadata.setMethodName("method1");
  metadata.requestHeaders().addCopy(Http::LowerCaseString{"header_name"}, "cluster1");

  EXPECT_TRUE(matcher.route(metadata, 0)->routeEntry()->stripServiceName());
}

} // namespace
} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
