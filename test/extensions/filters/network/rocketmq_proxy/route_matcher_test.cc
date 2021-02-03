#include "envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.h"
#include "envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/rocketmq_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/rocketmq_proxy/v3/route.pb.validate.h"

#include "extensions/filters/network/rocketmq_proxy/metadata.h"
#include "extensions/filters/network/rocketmq_proxy/router/route_matcher.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {
namespace Router {

using RouteConfigurationProto =
    envoy::extensions::filters::network::rocketmq_proxy::v3::RouteConfiguration;

RouteConfigurationProto parseRouteConfigurationFromV2Yaml(const std::string& yaml) {
  RouteConfigurationProto route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  TestUtility::validate(route_config);
  return route_config;
}

TEST(RocketmqRouteMatcherTest, RouteWithHeaders) {
  const std::string yaml = R"EOF(
name: default_route
routes:
  - match:
      topic:
        exact: test_topic
      headers:
        - name: code
          exact_match: '310'
    route:
      cluster: fake_cluster
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
)EOF";

  RouteConfigurationProto config = parseRouteConfigurationFromV2Yaml(yaml);

  MessageMetadata metadata;
  std::string topic_name = "test_topic";
  metadata.setTopicName(topic_name);
  uint64_t code = 310;
  metadata.headers().addCopy(Http::LowerCaseString("code"), code);
  RouteMatcher matcher(config);
  const Envoy::Router::MetadataMatchCriteria* criteria =
      matcher.route(metadata)->routeEntry()->metadataMatchCriteria();
  const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
      criteria->metadataMatchCriteria();

  ProtobufWkt::Value v1;
  v1.set_string_value("v1");
  HashedValue hv1(v1);

  EXPECT_EQ(1, mmc.size());
  EXPECT_EQ("k1", mmc[0]->name());
  EXPECT_EQ(hv1, mmc[0]->value());
}

} // namespace Router
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
