#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/ratelimit_config/ratelimit_config.h"

#include "test/extensions/filters/common/ratelimit_config/ratelimit_config_test.pb.h"
#include "test/extensions/filters/common/ratelimit_config/ratelimit_config_test.pb.validate.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {
namespace {

ProtoRateLimit parseRateLimitFromV3Yaml(const std::string& yaml_string) {
  ProtoRateLimit rate_limit;
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

class RateLimitConfigTest : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    test::extensions::filters::common::ratelimit_config::TestRateLimitConfig proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    config_ = std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::RateLimitConfig>(
        proto_config.rate_limits(), factory_context_, creation_status_);
    stream_info_.downstream_connection_info_provider_->setRemoteAddress(default_remote_address_);
    ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route_));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ProtobufMessage::NullValidationVisitorImpl any_validation_visitor_;
  absl::Status creation_status_{};
  std::unique_ptr<Envoy::Extensions::Filters::Common::RateLimit::RateLimitConfig> config_;
  Http::TestRequestHeaderMapImpl headers_;
  std::shared_ptr<Router::MockRoute> route_{new NiceMock<Router::MockRoute>()};
  Network::Address::InstanceConstSharedPtr default_remote_address_{
      new Network::Address::Ipv4Instance("10.0.0.1")};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(RateLimitConfigTest, DisableKeyIsNotAllowed) {
  {
    const std::string yaml = R"EOF(
  rate_limits:
  - actions:
    - remote_address: {}
    stage: 2
    disable_key: anything
    limit:
      dynamic_metadata:
        metadata_key:
          key: key
          path:
          - key: key
  )EOF";

    factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
    setupTest(yaml);
    EXPECT_FALSE(creation_status_.ok());
    EXPECT_EQ(creation_status_.message(),
              "'stage' field and 'disable_key' field are not supported");
  }
}

TEST_F(RateLimitConfigTest, LimitIsNotAllowed) {
  {
    const std::string yaml = R"EOF(
  rate_limits:
  - actions:
    - remote_address: {}
    limit:
      dynamic_metadata:
        metadata_key:
          key: key
          path:
          - key: key
  )EOF";

    factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
    setupTest(yaml);
    EXPECT_FALSE(creation_status_.ok());
    EXPECT_EQ(creation_status_.message(), "'limit' field is not supported");
  }
}

TEST_F(RateLimitConfigTest, NoAction) {
  {
    const std::string yaml = R"EOF(
actions:
- {}
  )EOF";

    ProtoRateLimit rate_limit;
    TestUtility::loadFromYaml(yaml, rate_limit);

    absl::Status creation_status;
    RateLimitPolicy policy(rate_limit, factory_context_, creation_status);

    EXPECT_TRUE(absl::StartsWith(creation_status.message(), "Unsupported rate limit action:"));
  }

  {
    const std::string yaml = R"EOF(
  rate_limits:
  - actions:
    - remote_address: {}
    - {}
  )EOF";

    factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
    setupTest(yaml);

    EXPECT_TRUE(absl::StartsWith(creation_status_.message(), "Unsupported rate limit action:"));
  }
}

TEST_F(RateLimitConfigTest, EmptyRateLimit) {
  const std::string yaml = R"EOF(
rate_limits: []
  )EOF";

  factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
  setupTest(yaml);

  EXPECT_TRUE(config_->empty());
}

TEST_F(RateLimitConfigTest, SinglePolicy) {
  const std::string yaml = R"EOF(
  rate_limits:
  - actions:
    - remote_address: {}
  )EOF";

  factory_context_.cluster_manager_.initializeClusters({"www2"}, {});
  setupTest(yaml);

  EXPECT_EQ(1U, config_->size());

  std::vector<Envoy::RateLimit::LocalDescriptor> descriptors;
  config_->populateDescriptors(headers_, stream_info_, "", descriptors);
  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));
}

TEST_F(RateLimitConfigTest, MultiplePoliciesAndMultipleActions) {
  const std::string yaml = R"EOF(
  rate_limits:
  - actions:
    - remote_address: {}
    - destination_cluster: {}
  - actions:
    - destination_cluster: {}
  )EOF";

  setupTest(yaml);

  std::vector<Envoy::RateLimit::LocalDescriptor> descriptors;

  config_->populateDescriptors(headers_, stream_info_, "", descriptors);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>(
                  {Envoy::RateLimit::LocalDescriptor{
                       {{"remote_address", "10.0.0.1"}, {"destination_cluster", "fake_cluster"}}},
                   Envoy::RateLimit::LocalDescriptor{{{"destination_cluster", "fake_cluster"}}}}),
              testing::ContainerEq(descriptors));
}

class RateLimitPolicyTest : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    rate_limit_entry_ = std::make_unique<RateLimitPolicy>(parseRateLimitFromV3Yaml(yaml),
                                                          factory_context_, creation_status_);
    descriptors_.clear();
    stream_info_.downstream_connection_info_provider_->setRemoteAddress(default_remote_address_);
    ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route_));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  std::unique_ptr<RateLimitPolicy> rate_limit_entry_;
  absl::Status creation_status_{};
  Http::TestRequestHeaderMapImpl headers_;
  std::shared_ptr<Router::MockRoute> route_{new NiceMock<Router::MockRoute>()};

  std::vector<Envoy::RateLimit::LocalDescriptor> descriptors_;
  Network::Address::InstanceConstSharedPtr default_remote_address_{
      new Network::Address::Ipv4Instance("10.0.0.1")};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

class RateLimitPolicyIpv6Test : public testing::Test {
public:
  void setupTest(const std::string& yaml) {
    absl::Status creation_status;
    rate_limit_entry_ = std::make_unique<RateLimitPolicy>(parseRateLimitFromV3Yaml(yaml),
                                                          factory_context_, creation_status);
    THROW_IF_NOT_OK(creation_status); // NOLINT
    descriptors_.clear();
    stream_info_.downstream_connection_info_provider_->setRemoteAddress(default_remote_address_);
    ON_CALL(Const(stream_info_), route()).WillByDefault(testing::Return(route_));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  std::unique_ptr<RateLimitPolicy> rate_limit_entry_;
  Http::TestRequestHeaderMapImpl headers_;
  std::vector<Envoy::RateLimit::LocalDescriptor> descriptors_;
  std::shared_ptr<Router::MockRoute> route_{new NiceMock<Router::MockRoute>()};

  Network::Address::InstanceConstSharedPtr default_remote_address_{
      new Network::Address::Ipv6Instance("2001:abcd:ef01:2345:6789:abcd:ef01:234")};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(RateLimitPolicyTest, RemoteAddress) {
  const std::string yaml = R"EOF(
actions:
- remote_address: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, MaskedRemoteAddressIpv4Default) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>(
                  {{{{"masked_remote_address", "10.0.0.1/32"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, MaskedRemoteAddressIpv4) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address:
    v4_prefix_mask_len: 16
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>(
                  {{{{"masked_remote_address", "10.0.0.0/16"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyIpv6Test, MaskedRemoteAddressIpv6Default) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>(
                  {{{{"masked_remote_address", "2001:abcd:ef01:2345:6789:abcd:ef01:234/128"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyIpv6Test, MaskedRemoteAddressIpv6) {
  const std::string yaml = R"EOF(
actions:
- masked_remote_address:
    v6_prefix_mask_len: 64
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>(
                  {{{{"masked_remote_address", "2001:abcd:ef01:2345::/64"}}}}),
              testing::ContainerEq(descriptors_));
}

// Verify no descriptor is emitted if remote is a pipe.
TEST_F(RateLimitPolicyTest, PipeAddress) {
  const std::string yaml = R"EOF(
actions:
- remote_address: {}
  )EOF";

  setupTest(yaml);

  stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Address::PipeInstance::create("/hello"));
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, SourceService) {
  const std::string yaml = R"EOF(
actions:
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, DestinationService) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"destination_cluster", "fake_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, RequestHeaders) {
  const std::string yaml = R"EOF(
actions:
- request_headers:
    header_name: x-header-name
    descriptor_key: my_header_name
  )EOF";

  setupTest(yaml);
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"my_header_name", "test_value"}}}}),
      testing::ContainerEq(descriptors_));
}

// Validate that a descriptor is added if the missing request header
// has skip_if_absent set to true
TEST_F(RateLimitPolicyTest, RequestHeadersWithSkipIfAbsent) {
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
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"my_header_name", "test_value"}}}}),
      testing::ContainerEq(descriptors_));
}

// Tests if the descriptors are added if one of the headers is missing
// and skip_if_absent is set to default value which is false
TEST_F(RateLimitPolicyTest, RequestHeadersWithDefaultSkipIfAbsent) {
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

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, RequestHeadersNoMatch) {
  const std::string yaml = R"EOF(
actions:
- request_headers:
    header_name: x-header
    descriptor_key: my_header_name
  )EOF";

  setupTest(yaml);
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, RateLimitKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_value: fake_key
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, GenericKeyWithSetDescriptorKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: fake_key
    descriptor_value: fake_value
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, GenericKeyWithEmptyDescriptorKey) {
  const std::string yaml = R"EOF(
actions:
- generic_key:
    descriptor_key: ""
    descriptor_value: fake_value
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"generic_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, MetaDataMatchDynamicSourceByDefault) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, MetaDataMatchDynamicSource) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, MetaDataMatchRouteEntrySource) {
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

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

// Tests that the default_value is used in the descriptor when the metadata_key is empty.
TEST_F(RateLimitPolicyTest, MetaDataNoMatchWithDefaultValue) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, MetaDataNoMatch) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, MetaDataEmptyValue) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

// Tests that no descriptors are generated when both the metadata_key and default_value are empty.
TEST_F(RateLimitPolicyTest, MetaDataAndDefaultValueEmpty) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

// Tests that no descriptor is generated when both the metadata_key and default_value are empty,
// and skip_if_absent is set to true.
TEST_F(RateLimitPolicyTest, MetaDataAndDefaultValueEmptySkipIfAbsent) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, MetaDataNonStringNoMatch) {
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
  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, HeaderValueMatch) {
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
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, HeaderValueMatchDescriptorKey) {
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
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, HeaderValueMatchNoMatch) {
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
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "not_same_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, HeaderValueMatchHeadersNotPresent) {
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
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "not_same_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, HeaderValueMatchHeadersPresent) {
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
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, QueryParameterValueMatch) {
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

  rate_limit_entry_->populateDescriptors(header, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"query_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, QueryParameterValueMatchDescriptorKey) {
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

  rate_limit_entry_->populateDescriptors(header, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"fake_key", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, QueryParameterValueMatchNoMatch) {
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

  rate_limit_entry_->populateDescriptors(header, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, QueryParameterValueMatchExpectNoMatch) {
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

  rate_limit_entry_->populateDescriptors(header, stream_info_, "", descriptors_);

  EXPECT_THAT(std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"query_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, QueryParameterValueMatchExpectNoMatchFailed) {
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

  rate_limit_entry_->populateDescriptors(header, stream_info_, "", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, CompoundActions) {
  const std::string yaml = R"EOF(
actions:
- destination_cluster: {}
- source_cluster: {}
  )EOF";

  setupTest(yaml);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>(
          {{{{"destination_cluster", "fake_cluster"}, {"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, CompoundActionsNoDescriptor) {
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

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

const std::string RequestHeaderMatchInputDescriptor = R"EOF(
actions:
- extension:
    name: my_header_name
    typed_config:
      "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
      header_name: x-header-name
  )EOF";

TEST_F(RateLimitPolicyTest, RequestMatchInput) {
  setupTest(RequestHeaderMatchInputDescriptor);
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_THAT(
      std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"my_header_name", "test_value"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyTest, RequestMatchInputEmpty) {
  setupTest(RequestHeaderMatchInputDescriptor);
  headers_.setCopy(Http::LowerCaseString("x-header-name"), "");

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_FALSE(descriptors_.empty());
}

TEST_F(RateLimitPolicyTest, RequestMatchInputSkip) {
  setupTest(RequestHeaderMatchInputDescriptor);

  rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);

  EXPECT_TRUE(descriptors_.empty());
}

class ExtensionDescriptorFactory : public Envoy::RateLimit::DescriptorProducerFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
  std::string name() const override { return "test.descriptor_producer"; }

  Envoy::RateLimit::DescriptorProducerPtr
  createDescriptorProducerFromProto(const Protobuf::Message&,
                                    Server::Configuration::CommonFactoryContext&) override {
    return return_valid_producer_ ? std::make_unique<Router::SourceClusterAction>() : nullptr;
  }
  bool return_valid_producer_{true};
};

TEST_F(RateLimitPolicyTest, ExtensionDescriptorProducer) {
  const std::string ExtensionDescriptor = R"EOF(
actions:
- extension:
    name: test.descriptor_producer
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        key: value
  )EOF";

  {
    ExtensionDescriptorFactory factory;
    Registry::InjectFactory<Envoy::RateLimit::DescriptorProducerFactory> registration(factory);

    setupTest(ExtensionDescriptor);

    rate_limit_entry_->populateDescriptors(headers_, stream_info_, "service_cluster", descriptors_);
    EXPECT_THAT(
        std::vector<Envoy::RateLimit::LocalDescriptor>({{{{"source_cluster", "service_cluster"}}}}),
        testing::ContainerEq(descriptors_));
  }

  {
    ExtensionDescriptorFactory factory;
    factory.return_valid_producer_ = false;
    Registry::InjectFactory<Envoy::RateLimit::DescriptorProducerFactory> registration(factory);

    setupTest(ExtensionDescriptor);

    EXPECT_TRUE(
        absl::StartsWith(creation_status_.message(), "Rate limit descriptor extension failed:"));
  }
}

} // namespace
} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
