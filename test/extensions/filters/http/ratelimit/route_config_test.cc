#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/router/config_impl.h"
#include "source/extensions/filters/http/ratelimit/ratelimit.h"

#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {
namespace {

Http::TestRequestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method) {
  return Http::TestRequestHeaderMapImpl{{":authority", host},
                                        {":path", path},
                                        {":method", method},
                                        {"x-forwarded-proto", "http"},
                                        {":scheme", "http"}};
}

class RateLimitConfiguration : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    envoy::config::route::v3::RouteConfiguration route_config;
    TestUtility::loadFromYaml(yaml, route_config);
    config_ = *Router::ConfigImpl::create(route_config, factory_context_, any_validation_visitor_,
                                          factory_context_.initManager(), true);
    stream_info_.downstream_connection_info_provider_->setRemoteAddress(default_remote_address_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ProtobufMessage::NullValidationVisitorImpl any_validation_visitor_;
  std::shared_ptr<Router::ConfigImpl> config_;
  Http::TestRequestHeaderMapImpl header_;
  Network::Address::InstanceConstSharedPtr default_remote_address_{
      new Network::Address::Ipv4Instance("10.0.0.1")};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(RateLimitConfiguration, NoApplicableRateLimit) {
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
    typed_per_filter_config:
      envoy.filters.http.ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ratelimit.v3.RateLimitPerRoute
        rate_limits:
        - actions:
          - remote_address: {}
  - match:
      prefix: "/bar"
    route:
      cluster: www2
  )EOF";

  factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
  setupTest(yaml);

  EXPECT_EQ(nullptr, config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info_, 0)
                         ->mostSpecificPerFilterConfig("envoy.filters.http.ratelimit"));
}

TEST_F(RateLimitConfiguration, TestGetApplicationRateLimit) {
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
    typed_per_filter_config:
      envoy.filters.http.ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ratelimit.v3.RateLimitPerRoute
        rate_limits:
        - actions:
          - remote_address: {}
  )EOF";

  factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
  setupTest(yaml);
  auto route = config_->route(genHeaders("www.lyft.com", "/foo", "GET"), stream_info_, 0);
  stream_info_.route_ = route;

  // The deprecated route level rate limits are not populated by the per filter config.
  EXPECT_TRUE(route->routeEntry()->rateLimitPolicy().empty());
  const auto* per_route = dynamic_cast<const FilterConfigPerRoute*>(
      route->mostSpecificPerFilterConfig("envoy.filters.http.ratelimit"));
  ASSERT_NE(nullptr, per_route);
  EXPECT_TRUE(per_route->hasRateLimitConfigs());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  per_route->populateDescriptors(header_, nullptr, stream_info_, descriptors, false);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));
}

TEST_F(RateLimitConfiguration, TestVirtualHost) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/"
    route:
      cluster: www2test
  typed_per_filter_config:
    envoy.filters.http.ratelimit:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ratelimit.v3.RateLimitPerRoute
      rate_limits:
      - actions:
        - destination_cluster: {}
  )EOF";

  factory_context_.cluster_manager_.initializeClusters({"www2test"}, {});
  setupTest(yaml);
  auto route = config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info_, 0);
  stream_info_.route_ = route;

  EXPECT_TRUE(route->virtualHost().rateLimitPolicy().empty());
  const auto* per_route = dynamic_cast<const FilterConfigPerRoute*>(
      route->virtualHost().mostSpecificPerFilterConfig("envoy.filters.http.ratelimit"));
  ASSERT_NE(nullptr, per_route);
  // The route inherits the virtual host config.
  EXPECT_EQ(per_route, route->mostSpecificPerFilterConfig("envoy.filters.http.ratelimit"));

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  per_route->populateDescriptors(header_, nullptr, stream_info_, descriptors, false);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"destination_cluster", "www2test"}}}}),
              testing::ContainerEq(descriptors));
}

TEST_F(RateLimitConfiguration, Stages) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: www2
  domains:
  - www.lyft.com
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster: www2test
    typed_per_filter_config:
      envoy.filters.http.ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ratelimit.v3.RateLimitPerRoute
        rate_limits:
        - stage: 1
          actions:
          - remote_address: {}
        - actions:
          - destination_cluster: {}
  )EOF";

  factory_context_.cluster_manager_.initializeClusters({"www2test"}, {});
  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  // Stages are only supported by the deprecated route level rate limits.
  auto config_or_error =
      Router::ConfigImpl::create(route_config, factory_context_, any_validation_visitor_,
                                 factory_context_.initManager(), true);
  ASSERT_FALSE(config_or_error.ok());
  EXPECT_THAT(config_or_error.status().message(),
              testing::HasSubstr("'stage' field and 'disable_key' field are not supported"));
}

} // namespace
} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
