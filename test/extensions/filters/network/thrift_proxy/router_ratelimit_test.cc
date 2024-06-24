#include <memory>

#include "envoy/common/exception.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/ratelimit/ratelimit.h"

#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/thrift_proxy/config.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/router/rds_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_ratelimit_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {
namespace {

class ThriftRateLimitConfigurationTest : public testing::Test {
public:
  ThriftRateLimitConfigurationTest() {
    route_config_provider_manager_ = std::make_unique<RouteConfigProviderManagerImpl>(
        factory_context_.server_factory_context_.admin_);
  }

  void initializeClusters(const std::vector<std::string>& cluster_names) {
    factory_context_.server_factory_context_.cluster_manager_.initializeClusters(cluster_names, {});
  }
  void initialize(envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config,
                  const std::vector<std::string>& cluster_names) {
    initializeClusters(cluster_names);
    initialize(config);
  }

  void initialize(const std::string& yaml, const std::vector<std::string>& cluster_names) {
    initializeClusters(cluster_names);
    initialize(yaml);
  }

  void initialize(const std::string& yaml) {
    envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config;
    TestUtility::loadFromYaml(yaml, config);
    initialize(config);
  }

  void initialize(envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config) {
    config_ = std::make_unique<ThriftProxy::ConfigImpl>(config, factory_context_,
                                                        *route_config_provider_manager_);
  }

  MessageMetadata& genMetadata(const std::string& method_name) {
    metadata_ = std::make_shared<MessageMetadata>();
    metadata_->setMethodName(method_name);
    return *metadata_;
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::unique_ptr<RouteConfigProviderManagerImpl> route_config_provider_manager_;
  std::unique_ptr<ThriftProxy::ConfigImpl> config_;
  Network::Address::Ipv4Instance default_remote_address_{"10.0.0.1"};
  MessageMetadataSharedPtr metadata_;
};

TEST_F(ThriftRateLimitConfigurationTest, NoApplicableRateLimit) {
  const std::string yaml = R"EOF(
route_config:
  name: config
  routes:
    - match: { method_name: "foo" }
      route:
        cluster: thrift
        rate_limits:
          - actions:
              - remote_address: {}
    - match: { method_name: "bar" }
      route: { cluster: thrift }
)EOF";
  initialize(yaml, {"thrift"});

  EXPECT_EQ(0U, config_->route(genMetadata("bar"), 0)
                    ->routeEntry()
                    ->rateLimitPolicy()
                    .getApplicableRateLimit(0)
                    .size());
}

TEST_F(ThriftRateLimitConfigurationTest, NoRateLimitPolicy) {
  const std::string yaml = R"EOF(
route_config:
  name: config
  routes:
    - match: { method_name: "bar" }
      route: { cluster: thrift }
)EOF";

  initialize(yaml, {"thrift"});

  auto route = config_->route(genMetadata("bar"), 0)->routeEntry();
  EXPECT_EQ(0U, route->rateLimitPolicy().getApplicableRateLimit(0).size());
  EXPECT_TRUE(route->rateLimitPolicy().empty());
}

TEST_F(ThriftRateLimitConfigurationTest, TestGetApplicableRateLimit) {
  const std::string yaml = R"EOF(
route_config:
  name: config
  routes:
    - match: { method_name: "foo" }
      route:
        cluster: thrift
        rate_limits:
          - actions:
              - remote_address: {}
)EOF";

  initialize(yaml, {"thrift"});

  auto route = config_->route(genMetadata("foo"), 0)->routeEntry();
  EXPECT_FALSE(route->rateLimitPolicy().empty());
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route, descriptors, "", *metadata_, default_remote_address_);
  }

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              ContainerEq(descriptors));
}

TEST_F(ThriftRateLimitConfigurationTest, Stages) {
  const std::string yaml = R"EOF(
route_config:
  name: config
  routes:
    - match: { method_name: "foo" }
      route:
        cluster: thrift
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

  initialize(yaml, {"thrift"});

  auto route = config_->route(genMetadata("foo"), 0)->routeEntry();
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(2U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route, descriptors, "service_cluster", *metadata_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"destination_cluster", "thrift"}}},
                   {{{"destination_cluster", "thrift"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors));

  descriptors.clear();
  rate_limits = route->rateLimitPolicy().getApplicableRateLimit(1);
  EXPECT_EQ(1U, rate_limits.size());

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route, descriptors, "service_cluster", *metadata_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));

  rate_limits = route->rateLimitPolicy().getApplicableRateLimit(10);
  EXPECT_TRUE(rate_limits.empty());
}

// Test that rate limiter stages work with weighted cluster route entries.
TEST_F(ThriftRateLimitConfigurationTest, WeightedClusterStages) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config;
  {
    auto* route_config = config.mutable_route_config();
    route_config->set_name("config");
    auto* route = route_config->add_routes();
    route->mutable_match()->set_method_name("foo");
    auto* action = route->mutable_route();
    auto* cluster1 = action->mutable_weighted_clusters()->add_clusters();
    cluster1->set_name("thrift");
    cluster1->mutable_weight()->set_value(50);
    auto* cluster2 = action->mutable_weighted_clusters()->add_clusters();
    cluster2->set_name("thrift2");
    cluster2->mutable_weight()->set_value(50);

    auto* limit1 = action->add_rate_limits();
    limit1->mutable_stage()->set_value(1);
    limit1->add_actions()->mutable_remote_address();

    action->add_rate_limits()->add_actions()->mutable_destination_cluster();

    auto* limit3 = action->add_rate_limits();
    limit3->add_actions()->mutable_destination_cluster();
    limit3->add_actions()->mutable_source_cluster();
  }
  initialize(config, {"thrift", "thrift2"});

  auto route = config_->route(genMetadata("foo"), 0)->routeEntry();
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(2U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route, descriptors, "service_cluster", *metadata_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"destination_cluster", "thrift"}}},
                   {{{"destination_cluster", "thrift"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors));

  descriptors.clear();
  rate_limits = route->rateLimitPolicy().getApplicableRateLimit(1);
  EXPECT_EQ(1U, rate_limits.size());

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route, descriptors, "service_cluster", *metadata_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));

  rate_limits = route->rateLimitPolicy().getApplicableRateLimit(10);
  EXPECT_TRUE(rate_limits.empty());
}

// Test that rate limiter stages work with dynamic route entries.
TEST_F(ThriftRateLimitConfigurationTest, ClusterHeaderStages) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config;
  {
    auto* route_config = config.mutable_route_config();
    route_config->set_name("config");
    auto* route = route_config->add_routes();
    route->mutable_match()->set_method_name("foo");
    auto* action = route->mutable_route();
    action->set_cluster_header("header_name");

    auto* limit1 = action->add_rate_limits();
    limit1->mutable_stage()->set_value(1);
    limit1->add_actions()->mutable_remote_address();

    action->add_rate_limits()->add_actions()->mutable_destination_cluster();

    auto* limit3 = action->add_rate_limits();
    limit3->add_actions()->mutable_destination_cluster();
    limit3->add_actions()->mutable_source_cluster();
  }
  initialize(config);

  auto& metadata = genMetadata("foo");
  metadata.requestHeaders().addCopy(Http::LowerCaseString{"header_name"}, "thrift");

  // Keep hold of route, it's a newly minted shared pointer.
  auto route = config_->route(metadata, 0);
  auto* route_entry = route->routeEntry();

  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_entry->rateLimitPolicy().getApplicableRateLimit(0);

  EXPECT_EQ(2U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_entry, descriptors, "service_cluster", *metadata_,
                                   default_remote_address_);
  }

  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"destination_cluster", "thrift"}}},
                   {{{"destination_cluster", "thrift"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors));

  descriptors.clear();
  rate_limits = route_entry->rateLimitPolicy().getApplicableRateLimit(1);
  EXPECT_EQ(1U, rate_limits.size());

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_entry, descriptors, "service_cluster", *metadata_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));

  rate_limits = route_entry->rateLimitPolicy().getApplicableRateLimit(10);
  EXPECT_TRUE(rate_limits.empty());
}

class ThriftRateLimitPolicyEntryTest : public testing::Test {
public:
  void initialize(const std::string& yaml) {
    envoy::config::route::v3::RateLimit rate_limit;
    TestUtility::loadFromYaml(yaml, rate_limit);

    rate_limit_entry_ = std::make_unique<RateLimitPolicyEntryImpl>(rate_limit, context_);
    descriptors_.clear();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<RateLimitPolicyEntryImpl> rate_limit_entry_;
  MessageMetadata metadata_;
  NiceMock<MockRouteEntry> route_;
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  Network::Address::Ipv4Instance default_remote_address_{"10.0.0.1"};
};

TEST_F(ThriftRateLimitPolicyEntryTest, RateLimitPolicyEntryMembers) {
  std::string yaml = R"EOF(
stage: 2
disable_key: "no_ratelimit"
actions:
  - remote_address: {}
  )EOF";

  initialize(yaml);

  EXPECT_EQ(2UL, rate_limit_entry_->stage());
  EXPECT_EQ("no_ratelimit", rate_limit_entry_->disableKey());
}

TEST_F(ThriftRateLimitPolicyEntryTest, RemoteAddressAction) {
  std::string yaml = R"EOF(
actions:
  - remote_address: {}
  )EOF";

  initialize(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, RemoteAddressActionNoDescriptorIfPipeAddr) {
  std::string yaml = R"EOF(
actions:
  - remote_address: {}
  )EOF";

  initialize(yaml);

  auto pipe_address = *Network::Address::PipeInstance::create("/hello");
  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", metadata_, *pipe_address);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(ThriftRateLimitPolicyEntryTest, SourceClusterAction) {
  std::string yaml = R"EOF(
actions:
  - source_cluster: {}
  )EOF";

  initialize(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, DestinationClusterAction) {
  std::string yaml = R"EOF(
actions:
  - destination_cluster: {}
  )EOF";

  initialize(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"destination_cluster", "fake_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, RequestHeadersAction) {
  std::string yaml = R"EOF(
actions:
  - request_headers:
      header_name: x-header-name
      descriptor_key: my_header_name
  )EOF";

  initialize(yaml);
  metadata_.requestHeaders().addCopy(Http::LowerCaseString{"x-header-name"}, "test_value");

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, RequestHeadersActionNoMatch) {
  std::string yaml = R"EOF(
actions:
  - request_headers:
      header_name: x-header-name
      descriptor_key: my_header_name
  )EOF";

  initialize(yaml);
  metadata_.requestHeaders().addCopy(Http::LowerCaseString{"x-not-header-name"}, "test_value");

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(ThriftRateLimitPolicyEntryTest, RequestHeadersActionMethodName) {
  std::string yaml = R"EOF(
actions:
  - request_headers:
      header_name: ":method-name"
      descriptor_key: method_name
  )EOF";

  initialize(yaml);
  metadata_.setMethodName("foo");

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"method_name", "foo"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, RequestHeadersActionMethodNameMissing) {
  std::string yaml = R"EOF(
actions:
  - request_headers:
      header_name: ":method-name"
      descriptor_key: method_name
  )EOF";

  initialize(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(ThriftRateLimitPolicyEntryTest, GenericKeyAction) {
  std::string yaml = R"EOF(
actions:
  - generic_key:
      descriptor_value: fake_key
  )EOF";

  initialize(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, HeaderValueActionMatch) {
  std::string yaml = R"EOF(
actions:
  - header_value_match:
      descriptor_value: fake_value
      headers:
        - name: x-header-name
          string_match:
            exact: test_value
  )EOF";

  initialize(yaml);
  metadata_.requestHeaders().addCopy(Http::LowerCaseString{"x-header-name"}, "test_value");

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, HeaderValueActionValueMismatch) {
  std::string yaml = R"EOF(
actions:
  - header_value_match:
      descriptor_value: fake_value
      headers:
        - name: x-header-name
          string_match:
            exact: test_value
  )EOF";

  initialize(yaml);
  metadata_.requestHeaders().addCopy(Http::LowerCaseString{"x-header-name"}, "not_test_value");

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(ThriftRateLimitPolicyEntryTest, HeaderValueActionNegateMatch) {
  std::string yaml = R"EOF(
actions:
  - header_value_match:
      descriptor_value: fake_value
      expect_match: false
      headers:
        - name: x-header-name
          string_match:
            exact: test_value
  )EOF";

  initialize(yaml);
  metadata_.requestHeaders().addCopy(Http::LowerCaseString{"x-header-name"}, "test_value");

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(ThriftRateLimitPolicyEntryTest, HeaderValueActionNegatedMatchProducesDescriptors) {
  std::string yaml = R"EOF(
actions:
  - header_value_match:
      descriptor_value: fake_value
      expect_match: false
      headers:
        - name: x-header-name
          string_match:
            exact: test_value
  )EOF";

  initialize(yaml);
  metadata_.requestHeaders().addCopy(Http::LowerCaseString{"x-header-name"}, "not_test_value");

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, CompoundAction) {
  std::string yaml = R"EOF(
actions:
  - destination_cluster: {}
  - source_cluster: {}
  )EOF";

  initialize(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>(
          {{{{"destination_cluster", "fake_cluster"}, {"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(ThriftRateLimitPolicyEntryTest, CompoundActionNoDescriptor) {
  std::string yaml = R"EOF(
actions:
  - destination_cluster: {}
  - header_value_match:
      descriptor_value: fake_value
      headers:
        - name: x-header-name
          string_match:
            exact: test_value
  )EOF";

  initialize(yaml);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", metadata_,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

} // namespace
} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
