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
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Router {
namespace {

envoy::config::route::v3::RateLimit parseRateLimitFromV3Yaml(const std::string& yaml_string,
                                                             bool avoid_boosting = true) {
  envoy::config::route::v3::RateLimit rate_limit;
  TestUtility::loadFromYaml(yaml_string, rate_limit, false, avoid_boosting);
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
    config_ = std::make_unique<ConfigImpl>(route_config, OptionalHttpFilters(), factory_context_,
                                           any_validation_visitor_, true);
    stream_info_.downstream_address_provider_->setRemoteAddress(default_remote_address_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ProtobufMessage::NullValidationVisitorImpl any_validation_visitor_;
  std::unique_ptr<ConfigImpl> config_;
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
  auto* route_entry = route->routeEntry();
  ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route));

  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_entry->virtualHost().rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  std::vector<Envoy::RateLimit::LocalDescriptor> local_descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(descriptors, "service_cluster", header_, stream_info_);
    rate_limit.populateLocalDescriptors(local_descriptors, "service_cluster", header_,
                                        stream_info_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"destination_cluster", "www2test"}}}}),
              testing::ContainerEq(descriptors));
  EXPECT_THAT(std::vector<Envoy::RateLimit::DescriptorEntry>({{"destination_cluster", "www2test"}}),
              testing::ContainerEq(local_descriptors.at(0).entries_));
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
  std::vector<Envoy::RateLimit::LocalDescriptor> local_descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(descriptors, "service_cluster", header_, stream_info_);
    rate_limit.populateLocalDescriptors(local_descriptors, "service_cluster", header_,
                                        stream_info_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"destination_cluster", "www2test"}}},
                   {{{"destination_cluster", "www2test"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>(
                  {{{{"destination_cluster", "www2test"}}},
                   {{{"destination_cluster", "www2test"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(local_descriptors));

  descriptors.clear();
  local_descriptors.clear();
  rate_limits = route_entry->rateLimitPolicy().getApplicableRateLimit(1UL);
  EXPECT_EQ(1U, rate_limits.size());

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(descriptors, "service_cluster", header_, stream_info_);
    rate_limit.populateLocalDescriptors(local_descriptors, "service_cluster", header_,
                                        stream_info_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(local_descriptors));
  rate_limits = route_entry->rateLimitPolicy().getApplicableRateLimit(10UL);
  EXPECT_TRUE(rate_limits.empty());
}

class RateLimitPolicyEntryTest : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    rate_limit_entry_ = std::make_unique<RateLimitPolicyEntryImpl>(
        parseRateLimitFromV3Yaml(yaml), ProtobufMessage::getStrictValidationVisitor());
    descriptors_.clear();
    local_descriptors_.clear();
    stream_info_.downstream_address_provider_->setRemoteAddress(default_remote_address_);
    ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route_));
  }

  std::unique_ptr<RateLimitPolicyEntryImpl> rate_limit_entry_;
  Http::TestRequestHeaderMapImpl header_;
  std::shared_ptr<MockRoute> route_{new NiceMock<MockRoute>()};
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  std::vector<Envoy::RateLimit::LocalDescriptor> local_descriptors_;
  Network::Address::InstanceConstSharedPtr default_remote_address_{
      new Network::Address::Ipv4Instance("10.0.0.1")};
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(local_descriptors_));
}

// Verify no descriptor is emitted if remote is a pipe.
TEST_F(RateLimitPolicyEntryTest, PipeAddress) {
  const std::string yaml = R"EOF(
actions:
- remote_address: {}
  )EOF";

  setupTest(yaml);

  stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::PipeInstance>("/hello"));
  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, SourceService) {
  const std::string yaml = R"EOF(
actions:
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header_,
                                              stream_info_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(local_descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, DestinationService) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header_,
                                              stream_info_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"destination_cluster", "fake_cluster"}}}}),
      testing::ContainerEq(descriptors_));
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"destination_cluster", "fake_cluster"}}}}),
      testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header,
                                              stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"my_header_name", "test_value"}}}}),
      testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header,
                                              stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"my_header_name", "test_value"}}}}),
      testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header,
                                              stream_info_);
  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header,
                                              stream_info_);
  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, RateLimitKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: fake_key
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "", header_, stream_info_);
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"generic_key", "fake_value"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
}
// Tests that no descriptor is generated when both the metadata_key and default_value are empty.
TEST_F(RateLimitPolicyEntryTest, MetaDataAndDefaultValueEmpty) {
  const std::string yaml = R"EOF(
actions:
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header_, stream_info_);

  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header, stream_info_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "", header, stream_info_);
  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, CompoundActions) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(descriptors_, "service_cluster", header_, stream_info_);
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header_,
                                              stream_info_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>(
          {{{{"destination_cluster", "fake_cluster"}, {"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>(
          {{{{"destination_cluster", "fake_cluster"}, {"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(local_descriptors_));
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
  rate_limit_entry_->populateLocalDescriptors(local_descriptors_, "service_cluster", header_,
                                              stream_info_);
  EXPECT_TRUE(descriptors_.empty());
  EXPECT_TRUE(local_descriptors_.empty());
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
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>(
          {{{{"generic_key", "limited_fake_key"}}, {{42, envoy::type::v3::RateLimitUnit::HOUR}}}}),
      testing::ContainerEq(descriptors_));
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

} // namespace
} // namespace Router
} // namespace Envoy
