#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/route/route.pb.validate.h"

#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"
#include "common/router/router_ratelimit.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Router {
namespace {

envoy::api::v2::route::RateLimit parseRateLimitFromV2Yaml(const std::string& yaml_string) {
  envoy::api::v2::route::RateLimit rate_limit;
  TestUtility::loadFromYaml(yaml_string, rate_limit);
  TestUtility::validate(rate_limit);
  return rate_limit;
}

TEST(BadRateLimitConfiguration, MissingActions) {
  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV2Yaml("{}"), EnvoyException,
                          "value must contain at least");
}

TEST(BadRateLimitConfiguration, ActionsMissingRequiredFields) {
  const std::string yaml_one = R"EOF(
actions:
- request_headers: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV2Yaml(yaml_one), EnvoyException,
                          "value length must be at least");

  const std::string yaml_two = R"EOF(
actions:
- request_headers:
    header_name: test
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV2Yaml(yaml_two), EnvoyException,
                          "value length must be at least");

  const std::string yaml_three = R"EOF(
actions:
- request_headers:
    descriptor_key: test
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV2Yaml(yaml_three), EnvoyException,
                          "value length must be at least");
}

static Http::TestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method) {
  return Http::TestHeaderMapImpl{
      {":authority", host}, {":path", path}, {":method", method}, {"x-forwarded-proto", "http"}};
}

class RateLimitConfiguration : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    envoy::api::v2::RouteConfiguration route_config;
    TestUtility::loadFromYaml(yaml, route_config);
    config_ =
        std::make_unique<ConfigImpl>(route_config, factory_context_, any_validation_visitor_, true);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ProtobufMessage::NullValidationVisitorImpl any_validation_visitor_;
  std::unique_ptr<ConfigImpl> config_;
  Http::TestHeaderMapImpl header_;
  const RouteEntry* route_;
  Network::Address::Ipv4Instance default_remote_address_{"10.0.0.1"};
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
      rate_limits:
      - actions:
        - remote_address: {}
  - match:
      prefix: "/bar"
    route:
      cluster: www2
  )EOF";

  setupTest(yaml);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  EXPECT_EQ(0U, config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info, 0)
                    ->routeEntry()
                    ->rateLimitPolicy()
                    .getApplicableRateLimit(0)
                    .size());
}

TEST_F(RateLimitConfiguration, NoRateLimitPolicy) {
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
  )EOF";

  setupTest(yaml);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  route_ = config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info, 0)->routeEntry();
  EXPECT_EQ(0U, route_->rateLimitPolicy().getApplicableRateLimit(0).size());
  EXPECT_TRUE(route_->rateLimitPolicy().empty());
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
      rate_limits:
      - actions:
        - remote_address: {}
  )EOF";

  setupTest(yaml);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  route_ = config_->route(genHeaders("www.lyft.com", "/foo", "GET"), stream_info, 0)->routeEntry();
  EXPECT_FALSE(route_->rateLimitPolicy().empty());
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "", header_, default_remote_address_);
  }
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
  rate_limits:
  - actions:
    - destination_cluster: {}
  )EOF";

  setupTest(yaml);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  route_ = config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info, 0)->routeEntry();
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_->virtualHost().rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "service_cluster", header_,
                                   default_remote_address_);
  }
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
      rate_limits:
      - stage: 1
        actions:
        - remote_address: {}
      - actions:
        - destination_cluster: {}
      - actions:
        - destination_cluster: {}
        - source_cluster: {}
  )EOF";

  setupTest(yaml);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  route_ = config_->route(genHeaders("www.lyft.com", "/foo", "GET"), stream_info, 0)->routeEntry();
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(2U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "service_cluster", header_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"destination_cluster", "www2test"}}},
                   {{{"destination_cluster", "www2test"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors));

  descriptors.clear();
  rate_limits = route_->rateLimitPolicy().getApplicableRateLimit(1UL);
  EXPECT_EQ(1U, rate_limits.size());

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "service_cluster", header_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));

  rate_limits = route_->rateLimitPolicy().getApplicableRateLimit(10UL);
  EXPECT_TRUE(rate_limits.empty());
}

class RateLimitPolicyEntryTest : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    rate_limit_entry_ = std::make_unique<RateLimitPolicyEntryImpl>(parseRateLimitFromV2Yaml(yaml));
    descriptors_.clear();
  }

  std::unique_ptr<RateLimitPolicyEntryImpl> rate_limit_entry_;
  Http::TestHeaderMapImpl header_;
  NiceMock<MockRouteEntry> route_;
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  Network::Address::Ipv4Instance default_remote_address_{"10.0.0.1"};
};

TEST_F(RateLimitPolicyEntryTest, RateLimitPolicyEntryMembers) {
  const std::string yaml = R"EOF(
stage: 2
disable_key: no_ratelimit
actions:
- remote_address: {}
  )EOF";

  setupTest(yaml);

  EXPECT_EQ(2UL, rate_limit_entry_->stage());
  EXPECT_EQ("no_ratelimit", rate_limit_entry_->disableKey());
}

TEST_F(RateLimitPolicyEntryTest, RemoteAddress) {
  const std::string yaml = R"EOF(
actions:
- remote_address: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors_));
}

// Verify no descriptor is emitted if remote is a pipe.
TEST_F(RateLimitPolicyEntryTest, PipeAddress) {
  const std::string yaml = R"EOF(
actions:
- remote_address: {}
  )EOF";

  setupTest(yaml);

  Network::Address::PipeInstance pipe_address("/hello");
  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_, pipe_address);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, SourceService) {
  const std::string yaml = R"EOF(
actions:
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, DestinationService) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"destination_cluster", "fake_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RequestHeaders) {
  const std::string yaml = R"EOF(
actions:
- request_headers:
    header_name: x-header-name
    descriptor_key: my_header_name
  )EOF";

  setupTest(yaml);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RequestHeadersNoMatch) {
  const std::string yaml = R"EOF(
actions:
- request_headers:
    header_name: x-header
    descriptor_key: my_header_name
  )EOF";

  setupTest(yaml);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, RateLimitKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: fake_key
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatch) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: fake_value
    headers:
    - name: x-header-name
      exact_match: test_value
  )EOF";

  setupTest(yaml);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchNoMatch) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: fake_value
    headers:
    - name: x-header-name
      exact_match: test_value
  )EOF";

  setupTest(yaml);
  Http::TestHeaderMapImpl header{{"x-header-name", "not_same_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchHeadersNotPresent) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: fake_value
    expect_match: false
    headers:
    - name: x-header-name
      exact_match: test_value
  )EOF";

  setupTest(yaml);
  Http::TestHeaderMapImpl header{{"x-header-name", "not_same_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchHeadersPresent) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: fake_value
    expect_match: false
    headers:
    - name: x-header-name
      exact_match: test_value
  )EOF";

  setupTest(yaml);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, CompoundActions) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>(
          {{{{"destination_cluster", "fake_cluster"}, {"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, CompoundActionsNoDescriptor) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
- header_value_match:
    descriptor_value: fake_value
    headers:
    - name: x-header-name
      exact_match: test_value
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

} // namespace
} // namespace Router
} // namespace Envoy
