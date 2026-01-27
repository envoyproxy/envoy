#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/router_ratelimit.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Router {
namespace {

envoy::config::route::v3::RateLimit parseRateLimitFromV3Yaml(const std::string& yaml_string) {
  envoy::config::route::v3::RateLimit rate_limit;
  TestUtility::loadFromYaml(yaml_string, rate_limit);
  TestUtility::validate(rate_limit);
  return rate_limit;
}

TEST(BadRateLimitConfiguration, MissingActions) {
  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV3Yaml("{}"), EnvoyException,
                          "value must contain at least");
}

TEST(BadRateLimitConfiguration, ActionsMissingRequiredFields) {
  const std::string yaml_one = R"EOF(
actions:
- request_headers: {}
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV3Yaml(yaml_one), EnvoyException,
                          "value length must be at least");

  const std::string yaml_two = R"EOF(
actions:
- request_headers:
    header_name: test
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV3Yaml(yaml_two), EnvoyException,
                          "value length must be at least");

  const std::string yaml_three = R"EOF(
actions:
- request_headers:
    descriptor_key: test
  )EOF";

  EXPECT_THROW_WITH_REGEX(parseRateLimitFromV3Yaml(yaml_three), EnvoyException,
                          "value length must be at least");
}

static Http::TestRequestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
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
    config_ = *ConfigImpl::create(route_config, factory_context_, any_validation_visitor_, true);
    stream_info_.downstream_connection_info_provider_->setRemoteAddress(default_remote_address_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ProtobufMessage::NullValidationVisitorImpl any_validation_visitor_;
  std::shared_ptr<ConfigImpl> config_;
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

  EXPECT_EQ(0U, config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info_, 0)
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

  factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
  setupTest(yaml);
  auto route = config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info_, 0);
  auto* route_entry = route->routeEntry();
  ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route));

  EXPECT_EQ(0U, route_entry->rateLimitPolicy().getApplicableRateLimit(0).size());
  EXPECT_TRUE(route_entry->rateLimitPolicy().empty());
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

  factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
  setupTest(yaml);
  auto route = config_->route(genHeaders("www.lyft.com", "/foo", "GET"), stream_info_, 0);
  auto* route_entry = route->routeEntry();
  ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route));

  EXPECT_FALSE(route_entry->rateLimitPolicy().empty());
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_entry->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(descriptors, "", header_, stream_info_);
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

  factory_context_.cluster_manager_.initializeClusters({"www2test"}, {});
  setupTest(yaml);
  auto route = config_->route(genHeaders("www.lyft.com", "/bar", "GET"), stream_info_, 0);
  ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route));

  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route->virtualHost()->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(descriptors, "service_cluster", header_, stream_info_);
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

  factory_context_.cluster_manager_.initializeClusters({"www2test"}, {});
  setupTest(yaml);
  auto route = config_->route(genHeaders("www.lyft.com", "/foo", "GET"), stream_info_, 0);
  auto* route_entry = route->routeEntry();
  ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route));

  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_entry->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(2U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(descriptors, "service_cluster", header_, stream_info_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"destination_cluster", "www2test"}}},
                   {{{"destination_cluster", "www2test"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors));

  descriptors.clear();
  rate_limits = route_entry->rateLimitPolicy().getApplicableRateLimit(1UL);
  EXPECT_EQ(1U, rate_limits.size());

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(descriptors, "service_cluster", header_, stream_info_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));
  rate_limits = route_entry->rateLimitPolicy().getApplicableRateLimit(10UL);
  EXPECT_TRUE(rate_limits.empty());
}

class RateLimitPolicyEntryTest : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    absl::Status creation_status;
    rate_limit_entry_ = std::make_unique<RateLimitPolicyEntryImpl>(
        parseRateLimitFromV3Yaml(yaml), factory_context_, creation_status);
    THROW_IF_NOT_OK(creation_status); // NOLINT
    descriptors_.clear();
    stream_info_.downstream_connection_info_provider_->setRemoteAddress(default_remote_address_);
    ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route_));
  }

  TestScopedRuntime scoped_runtime_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ScopedThreadLocalServerContextSetter server_context_singleton_setter_{factory_context_};
  std::unique_ptr<RateLimitPolicyEntryImpl> rate_limit_entry_;
  Http::TestRequestHeaderMapImpl header_;
  std::shared_ptr<MockRoute> route_{new NiceMock<MockRoute>()};
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  Network::Address::InstanceConstSharedPtr default_remote_address_{
      new Network::Address::Ipv4Instance("10.0.0.1")};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

class RateLimitPolicyEntryIpv6Test : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    absl::Status creation_status;
    rate_limit_entry_ = std::make_unique<RateLimitPolicyEntryImpl>(
        parseRateLimitFromV3Yaml(yaml), factory_context_, creation_status);
    THROW_IF_NOT_OK(creation_status); // NOLINT
    descriptors_.clear();
    stream_info_.downstream_connection_info_provider_->setRemoteAddress(default_remote_address_);
    ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route_));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ScopedThreadLocalServerContextSetter server_context_singleton_setter_{factory_context_};
  std::unique_ptr<RateLimitPolicyEntryImpl> rate_limit_entry_;
  Http::TestRequestHeaderMapImpl header_;
  std::shared_ptr<MockRoute> route_{new NiceMock<MockRoute>()};
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  Network::Address::InstanceConstSharedPtr default_remote_address_{
      new Network::Address::Ipv6Instance("2001:abcd:ef01:2345:6789:abcd:ef01:234")};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
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

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, MaskedRemoteAddressIpv4Default) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"masked_remote_address", "10.0.0.1/32"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, MaskedRemoteAddressIpv4) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address:
    v4_prefix_mask_len: 16
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"masked_remote_address", "10.0.0.0/16"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryIpv6Test, MaskedRemoteAddressIpv6Default) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"masked_remote_address", "2001:abcd:ef01:2345:6789:abcd:ef01:234/128"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryIpv6Test, MaskedRemoteAddressIpv6) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address:
    v6_prefix_mask_len: 64
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"masked_remote_address", "2001:abcd:ef01:2345::/64"}}}}),
              testing::ContainerEq(descriptors_));
}

// Verify no descriptor is emitted if remote is a pipe.
TEST_F(RateLimitPolicyEntryTest, PipeAddress) {
  const std::string yaml = R"EOF(
actions:
- remote_address: {}
  )EOF";

  setupTest(yaml);

  stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Address::PipeInstance::create("/hello"));
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, SourceService) {
  const std::string yaml = R"EOF(
actions:
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
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

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
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
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

// Validate that a descriptor is added if the missing request header
// has skip_if_absent set to true
TEST_F(RateLimitPolicyEntryTest, RequestHeadersWithSkipIfAbsent) {
  const std::string yaml = R"EOF(
actions:
- request_headers:
    header_name: x-header-name
    descriptor_key: my_header_name
    skip_if_absent: false
- request_headers:
    header_name: x-header
    descriptor_key: my_header
    skip_if_absent: true
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

// Tests if the descriptors are added if one of the headers is missing
// and skip_if_absent is set to default value which is false
TEST_F(RateLimitPolicyEntryTest, RequestHeadersWithDefaultSkipIfAbsent) {
  const std::string yaml = R"EOF(
actions:
- request_headers:
    header_name: x-header-name
    descriptor_key: my_header_name
    skip_if_absent: false
- request_headers:
    header_name: x-header
    descriptor_key: my_header
    skip_if_absent: false
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-test", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, RequestHeadersNoMatch) {
  const std::string yaml = R"EOF(
actions:
- request_headers:
    header_name: x-header
    descriptor_key: my_header_name
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, RateLimitKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: fake_key
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_EQ(descriptors_[0].x_ratelimit_option_, envoy::config::route::v3::RateLimit::UNSPECIFIED);
}

TEST_F(RateLimitPolicyEntryTest, RateLimitKeyWithXRateLimitOptionOff) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: fake_key
x_ratelimit_option: "OFF"
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_EQ(descriptors_[0].x_ratelimit_option_, envoy::config::route::v3::RateLimit::OFF);
}

TEST_F(RateLimitPolicyEntryTest, RateLimitKeyWithXRateLimitOption) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: fake_key
x_ratelimit_option: DRAFT_VERSION_03
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_EQ(descriptors_[0].x_ratelimit_option_,
            envoy::config::route::v3::RateLimit::DRAFT_VERSION_03);
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyWithSetDescriptorKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: fake_key
    descriptor_value: fake_value
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyWithEmptyDescriptorKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: ""
    descriptor_value: fake_value
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, DEPRECATED_FEATURE_TEST(DynamicMetaDataMatch)) {
  const std::string yaml = R"EOF(
actions:
- dynamic_metadata:
    descriptor_key: fake_key
    default_value: fake_value
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    test:
      prop: foo
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, MetaDataMatchDynamicSourceByDefault) {
  const std::string yaml = R"EOF(
actions:
- metadata:
    descriptor_key: fake_key
    default_value: fake_value
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    test:
      prop: foo
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, MetaDataMatchDynamicSource) {
  const std::string yaml = R"EOF(
actions:
- metadata:
    descriptor_key: fake_key
    default_value: fake_value
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
    source: DYNAMIC
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    test:
      prop: foo
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, MetaDataMatchRouteEntrySource) {
  const std::string yaml = R"EOF(
actions:
- metadata:
    descriptor_key: fake_key
    default_value: fake_value
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
    source: ROUTE_ENTRY
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    test:
      prop: foo
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, route_->metadata_);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

// Tests that the default_value is used in the descriptor when the metadata_key is empty.
TEST_F(RateLimitPolicyEntryTest, MetaDataNoMatchWithDefaultValue) {
  const std::string yaml = R"EOF(
actions:
- metadata:
    descriptor_key: fake_key
    default_value: fake_value
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    another_key:
      prop: foo
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, MetaDataNoMatch) {
  const std::string yaml = R"EOF(
actions:
- metadata:
    descriptor_key: fake_key
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    another_key:
      prop: foo
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, MetaDataEmptyValue) {
  const std::string yaml = R"EOF(
actions:
- metadata:
    descriptor_key: fake_key
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    test:
      prop: ""
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
}

// Tests that no descriptors are generated when both the metadata_key and default_value are empty.
TEST_F(RateLimitPolicyEntryTest, MetaDataAndDefaultValueEmpty) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: fake_key
    descriptor_value: fake_value
- metadata:
    descriptor_key: fake_key
    default_value: ""
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    another_key:
      prop: ""
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
}

// Tests that no descriptor is generated when both the metadata_key and default_value are empty,
// and skip_if_absent is set to true.
TEST_F(RateLimitPolicyEntryTest, MetaDataAndDefaultValueEmptySkipIfAbsent) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: fake_key
    descriptor_value: fake_value
- metadata:
    descriptor_key: fake_key
    default_value: ""
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
    skip_if_absent: true
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    another_key:
      prop: ""
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, MetaDataNonStringNoMatch) {
  const std::string yaml = R"EOF(
actions:
- metadata:
    descriptor_key: fake_key
    metadata_key:
      key: 'envoy.xxx'
      path:
      - key: test
      - key: prop
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  envoy.xxx:
    test:
      prop:
        foo: bar
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatch) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: fake_value
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchDescriptorKey) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_key: fake_key
    descriptor_value: fake_value
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchNoMatch) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: fake_value
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "not_same_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
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
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "not_same_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
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
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatch) {
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: fake_value
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"query_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchDescriptorKey) {
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_key: fake_key
    descriptor_value: fake_value
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchNoMatch) {
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: fake_value
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=not_same_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchExpectNoMatch) {
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: fake_value
    expect_match: false
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=not_same_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"query_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchExpectNoMatchFailed) {
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: fake_value
    expect_match: false
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, CompoundActions) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
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
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, DynamicMetadataRateLimitOverride) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: limited_fake_key
limit:
 dynamic_metadata:
   metadata_key:
     key: test.filter.key
     path:
      - key: test
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  test.filter.key:
    test:
      requests_per_unit: 42
      unit: HOUR
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);

  auto descriptors =
      std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "limited_fake_key"}}}});
  descriptors[0].limit_ = {42, envoy::type::v3::RateLimitUnit::HOUR};
  EXPECT_THAT(descriptors, testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, DynamicMetadataRateLimitOverrideNotFound) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: limited_fake_key
limit:
 dynamic_metadata:
   metadata_key:
     key: unknown.key
     path:
      - key: test
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  test.filter.key:
    test:
      requests_per_unit: 42
      unit: HOUR
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "limited_fake_key"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, DynamicMetadataRateLimitOverrideWrongType) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: limited_fake_key
limit:
 dynamic_metadata:
   metadata_key:
     key: test.filter.key
     path:
      - key: test
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  test.filter.key:
    test: some_string
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "limited_fake_key"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, DynamicMetadataRateLimitOverrideWrongUnit) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: limited_fake_key
limit:
 dynamic_metadata:
   metadata_key:
     key: test.filter.key
     path:
      - key: test
  )EOF";

  setupTest(yaml);

  std::string metadata_yaml = R"EOF(
filter_metadata:
  test.filter.key:
    test:
      requests_per_unit: 42
      unit: NOT_A_UNIT
  )EOF";

  TestUtility::loadFromYaml(metadata_yaml, stream_info_.dynamicMetadata());
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "limited_fake_key"}}}}),
              testing::ContainerEq(descriptors_));
}

const std::string RequestHeaderMatchInputDescriptor = R"EOF(
actions:
- extension:
    name: my_header_name
    typed_config:
      "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
      header_name: x-header-name
  )EOF";

TEST_F(RateLimitPolicyEntryTest, RequestMatchInput) {
  setupTest(RequestHeaderMatchInputDescriptor);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RequestMatchInputEmpty) {
  setupTest(RequestHeaderMatchInputDescriptor);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", ""}};

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header, stream_info_);
  EXPECT_FALSE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, RequestMatchInputSkip) {
  setupTest(RequestHeaderMatchInputDescriptor);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, QueryParametersBasicMatch) {
  const std::string yaml = R"EOF(
actions:
- query_parameters:
    query_parameter_name: x-parameter-name
    descriptor_key: my_param
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_param", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParametersSkipIfAbsentFalse) {
  const std::string yaml = R"EOF(
actions:
- query_parameters:
    query_parameter_name: x-parameter-name
    descriptor_key: my_param
    skip_if_absent: false
  )EOF";

  setupTest(yaml);
  // No matching query parameter
  Http::TestRequestHeaderMapImpl header{{":path", "/no-match"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, QueryParametersSkipIfAbsentTrue) {
  const std::string yaml = R"EOF(
actions:
- query_parameters:
    query_parameter_name: x-parameter-name
    descriptor_key: my_param
    skip_if_absent: true
  )EOF";

  setupTest(yaml);
  // No matching query parameter
  Http::TestRequestHeaderMapImpl header{{":path", "/no-match"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  // Descriptor should be added even if the query parameter is not present
  EXPECT_FALSE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, QueryParametersMultipleValues) {
  const std::string yaml = R"EOF(
actions:
- query_parameters:
    query_parameter_name: x-parameter-name
    descriptor_key: my_param
  )EOF";

  setupTest(yaml);
  // Multiple values for the same query parameter
  Http::TestRequestHeaderMapImpl header{
      {":path", "/?x-parameter-name=value1&x-parameter-name=value2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_param", "value1"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParametersUrlEncoding) {
  const std::string yaml = R"EOF(
actions:
- query_parameters:
    query_parameter_name: test-parameter
    descriptor_key: my_param
  )EOF";

  setupTest(yaml);
  // URL-encoded query parameter
  Http::TestRequestHeaderMapImpl header{{":path", "/?test-parameter=hello%20world"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_param", "hello world"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormat) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: "%REQ(x-custom-header)%"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-custom-header", "custom_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "custom_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatCEL) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: "%CEL(request.headers['user-type'])%"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"user-type", "premium"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "premium"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatCELHeaderMissing) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: "%CEL(request.headers['user-type'])%"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{}};

  // When header is missing, CEL returns empty/null, descriptor is skipped
  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatCELRegexExtract) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: "user_id_key"
    descriptor_value: "%CEL(re.extract(request.headers['x-user-context'], '^id:([a-zA-Z0-9]+),', '\\\\1'))%"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-user-context", "id:abc123,tenant:prod"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"user_id_key", "abc123"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatCELRegexExtractHeaderMissing) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: "user_id_key"
    descriptor_value: "%CEL(re.extract(request.headers['x-user-context'], '^id:([a-zA-Z0-9]+),', '\\\\1'))%"
    default_value: "unknown"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  // With default_value set to non-empty string, descriptor is created with the default value
  EXPECT_THAT(descriptors_, testing::ContainerEq(std::vector<Envoy::RateLimit::Descriptor>(
                                {{{{"user_id_key", "unknown"}}}})));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatCELWithDefaultValue) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: "user_id_key"
    descriptor_value: "%CEL(re.extract(request.headers['x-user-context'], '^id:([a-zA-Z0-9]+),', '\\\\1'))%"
    default_value: "anonymous"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"user_id_key", "anonymous"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatCELWithoutDefaultValueSkipsDescriptor) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: "user_id_key"
    descriptor_value: "%CEL(re.extract(request.headers['x-user-context'], '^id:([a-zA-Z0-9]+),', '\\\\1'))%"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  // Descriptor should be skipped when formatting fails and no default_value is set
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchDescriptorFormatWithDefaultValue) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%CEL(re.extract(request.headers['x-user-id'], '^user-([0-9]+)', '\\\\1'))%"
    descriptor_key: "user_match"
    default_value: "unknown"
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"user_match", "unknown"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchDescriptorFormatWithDefaultValueSuccess) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%CEL(re.extract(request.headers['x-user-id'], '^user-([0-9]+)', '\\\\1'))%"
    descriptor_key: "user_match"
    default_value: "unknown"
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}, {"x-user-id", "user-123"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"user_match", "123"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchDescriptorFormatWithDefaultValue) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%CEL(re.extract(request.headers['x-api-key'], '^key-([a-z]+)', '\\\\1'))%"
    descriptor_key: "api_key"
    default_value: "default_key"
    query_parameters:
    - name: action
      string_match:
        exact: query_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/path?action=query_value"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"api_key", "default_key"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatPlainString) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: "static_key"
    descriptor_value: "static_value"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-test", "test"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"static_key", "static_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchDescriptorFormat) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%REQ(x-user-id)%"
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}, {"x-user-id", "123"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "123"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchDescriptorFormatCEL) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%CEL(request.headers['x-tier'])%"
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}, {"x-tier", "premium"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "premium"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchDescriptorFormatCELHeaderMissing) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%CEL(request.headers['x-tier'])%"
    headers:
    - name: x-header-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header-name", "test_value"}};

  // When header is missing, CEL returns empty/null, descriptor is skipped
  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchDescriptorFormat) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%REQ(x-api-key)%"
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=test_value"},
                                        {"x-api-key", "secret123"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"query_match", "secret123"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchDescriptorFormatCEL) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%CEL(request.headers['x-region'])%"
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=test_value"},
                                        {"x-region", "eu-central"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"query_match", "eu-central"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchDescriptorFormatCELHeaderMissing) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%CEL(request.headers['x-region'])%"
    query_parameters:
    - name: x-parameter-name
      string_match:
        exact: test_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?x-parameter-name=test_value"}};

  // When header is missing, CEL returns empty/null, descriptor is skipped
  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchDescriptorFormatWithDefaultValueSuccess) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%CEL(re.extract(request.headers['x-api-key'], '^key-([a-z]+)', '\\\\1'))%"
    descriptor_key: "api_key"
    default_value: "default_key"
    query_parameters:
    - name: action
      string_match:
        exact: query_value
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/path?action=query_value"},
                                        {"x-api-key", "key-alpha"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"api_key", "alpha"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyDescriptorFormatREQWithDefaultValue) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: "method_key"
    descriptor_value: "%REQ(x-custom-method)%"
    default_value: "GET"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"method_key", "GET"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchDescriptorFormatREQWithDefaultValue) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%REQ(x-priority)%"
    descriptor_key: "priority"
    default_value: "normal"
    headers:
    - name: x-service
      string_match:
        exact: api
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-service", "api"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"priority", "normal"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchDescriptorFormatREQWithDefaultValue) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%REQ(x-client-type)%"
    descriptor_key: "client_type"
    default_value: "web"
    query_parameters:
    - name: version
      string_match:
        exact: v2
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{":path", "/?version=v2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"client_type", "web"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyMixedStaticAndDynamicFormatters) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header1", "dynamic1"}, {"x-header2", "dynamic2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);

  // Multiple formatters with static text should concatenate properly
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"generic_key", "dynamic1_static_value_dynamic2"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, GenericKeyFormatterDisabled) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{{"x-header1", "dynamic1"}, {"x-header2", "dynamic2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);

  // With formatter disabled (default), descriptor_value should be used as literal string
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"generic_key",
                      "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchMixedStaticAndDynamicFormatters) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"
    headers:
    - name: x-service
      string_match:
        exact: api
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{
      {"x-service", "api"}, {"x-header1", "dynamic1"}, {"x-header2", "dynamic2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);

  // Multiple formatters with static text should concatenate properly
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"header_match", "dynamic1_static_value_dynamic2"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchFormatterDisabled) {
  const std::string yaml = R"EOF(
actions:
- header_value_match:
    descriptor_value: "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"
    headers:
    - name: x-service
      string_match:
        exact: api
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{
      {"x-service", "api"}, {"x-header1", "dynamic1"}, {"x-header2", "dynamic2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);

  // With formatter disabled (default), descriptor_value should be used as literal string
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"header_match",
                      "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchMixedStaticAndDynamicFormatters) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.enable_formatter_for_ratelimit_action_descriptor_value",
        "true"}});
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"
    query_parameters:
    - name: version
      string_match:
        exact: v2
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{
      {":path", "/?version=v2"}, {"x-header1", "dynamic1"}, {"x-header2", "dynamic2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);

  // Multiple formatters with static text should concatenate properly
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"query_match", "dynamic1_static_value_dynamic2"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, QueryParameterValueMatchFormatterDisabled) {
  const std::string yaml = R"EOF(
actions:
- query_parameter_value_match:
    descriptor_value: "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"
    query_parameters:
    - name: version
      string_match:
        exact: v2
  )EOF";

  setupTest(yaml);
  Http::TestRequestHeaderMapImpl header{
      {":path", "/?version=v2"}, {"x-header1", "dynamic1"}, {"x-header2", "dynamic2"}};

  rate_limit_entry_->populateDescriptors(descriptors_, "", header, stream_info_);

  // With formatter disabled (default), descriptor_value should be used as literal string
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"query_match",
                      "%REQ(x-header1)%_static_value_%CEL(request.headers['x-header2'])%"}}}}),
              testing::ContainerEq(descriptors_));
}

} // namespace
} // namespace Router
} // namespace Envoy
