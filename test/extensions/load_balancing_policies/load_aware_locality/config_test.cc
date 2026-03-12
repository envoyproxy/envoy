#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/cluster_provided/v3/cluster_provided.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {
namespace {

envoy::config::core::v3::TypedExtensionConfig
typedExtensionConfig(const std::string& name, const Protobuf::Message& message) {
  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name(name);
  config.mutable_typed_config()->PackFrom(message);
  return config;
}

envoy::config::core::v3::TypedExtensionConfig roundRobinEndpointPolicy() {
  envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin round_robin;
  return typedExtensionConfig("envoy.load_balancing_policies.round_robin", round_robin);
}

LoadAwareLocalityLbProto
loadAwareConfig(std::initializer_list<envoy::config::core::v3::TypedExtensionConfig> children) {
  LoadAwareLocalityLbProto config;
  for (const auto& child : children) {
    *config.mutable_endpoint_picking_policy()->add_policies()->mutable_typed_extension_config() =
        child;
  }
  return config;
}

const LoadAwareLocalityLbConfig& typedConfig(const Upstream::LoadBalancerConfigPtr& config) {
  const auto* typed = dynamic_cast<const LoadAwareLocalityLbConfig*>(config.get());
  EXPECT_NE(nullptr, typed);
  return *typed;
}

void expectFactoryCreateSucceeds(const Upstream::LoadBalancerConfigPtr& config,
                                 Server::Configuration::MockServerFactoryContext& context) {
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> priority_set;
  NiceMock<Event::MockDispatcher> dispatcher;
  ON_CALL(context, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher));

  Factory factory;
  auto lb = factory.create(*config, cluster_info, priority_set, context.runtime_loader_,
                           context.api_.random_, context.time_system_);
  ASSERT_NE(nullptr, lb);
  ASSERT_TRUE(lb->initialize().ok());

  auto worker_factory = lb->factory();
  ASSERT_NE(nullptr, worker_factory);
  EXPECT_NE(nullptr, worker_factory->create({priority_set, nullptr}));
}

envoy::config::core::v3::TypedExtensionConfig unsupportedChildPolicy(absl::string_view name) {
  if (name == "envoy.load_balancing_policies.maglev") {
    envoy::extensions::load_balancing_policies::maglev::v3::Maglev maglev;
    return typedExtensionConfig(std::string(name), maglev);
  }
  if (name == "envoy.load_balancing_policies.ring_hash") {
    envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash ring_hash;
    return typedExtensionConfig(std::string(name), ring_hash);
  }

  envoy::extensions::load_balancing_policies::cluster_provided::v3::ClusterProvided
      cluster_provided;
  return typedExtensionConfig(std::string(name), cluster_provided);
}

class UnsupportedChildPolicyTest : public testing::TestWithParam<absl::string_view> {};

TEST(LoadAwareLocalityConfigTest, Defaults) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Event::MockDispatcher> dispatcher;
  ON_CALL(context, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher));

  Factory factory;
  auto result = factory.loadConfig(context, loadAwareConfig({roundRobinEndpointPolicy()}));
  ASSERT_TRUE(result.ok());

  const auto& config = typedConfig(result.value());
  EXPECT_EQ(std::chrono::milliseconds(1000), config.weightUpdatePeriod());
  EXPECT_DOUBLE_EQ(0.1, config.utilizationVarianceThreshold());
  EXPECT_DOUBLE_EQ(0.3, config.ewmaAlpha());
  EXPECT_DOUBLE_EQ(0.03, config.probePercentage());
  EXPECT_EQ(std::chrono::milliseconds(180000), config.weightExpirationPeriod());

  expectFactoryCreateSucceeds(result.value(), context);
}

TEST(LoadAwareLocalityConfigTest, Overrides) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Event::MockDispatcher> dispatcher;
  ON_CALL(context, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher));

  auto config_proto = loadAwareConfig({roundRobinEndpointPolicy()});
  config_proto.mutable_weight_update_period()->set_seconds(2);
  config_proto.mutable_utilization_variance_threshold()->set_value(0.05);
  config_proto.mutable_ewma_alpha()->set_value(0.5);
  config_proto.mutable_probe_percentage()->set_value(0.07);
  config_proto.mutable_weight_expiration_period()->set_seconds(15);

  Factory factory;
  auto result = factory.loadConfig(context, config_proto);
  ASSERT_TRUE(result.ok());

  const auto& config = typedConfig(result.value());
  EXPECT_EQ(std::chrono::milliseconds(2000), config.weightUpdatePeriod());
  EXPECT_DOUBLE_EQ(0.05, config.utilizationVarianceThreshold());
  EXPECT_DOUBLE_EQ(0.5, config.ewmaAlpha());
  EXPECT_DOUBLE_EQ(0.07, config.probePercentage());
  EXPECT_EQ(std::chrono::milliseconds(15000), config.weightExpirationPeriod());

  expectFactoryCreateSucceeds(result.value(), context);
}

TEST_P(UnsupportedChildPolicyTest, UnsupportedOrMissingChild) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  LoadAwareLocalityLbProto config_proto =
      GetParam().empty() ? loadAwareConfig({})
                         : loadAwareConfig({unsupportedChildPolicy(GetParam())});

  Factory factory;
  auto result = factory.loadConfig(context, config_proto);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  if (GetParam().empty()) {
    EXPECT_EQ(result.status(), absl::InvalidArgumentError("No supported endpoint picking policy."));
  } else {
    EXPECT_THAT(result.status().message(), testing::HasSubstr(std::string(GetParam())));
  }
}

INSTANTIATE_TEST_SUITE_P(
    ChildPolicies, UnsupportedChildPolicyTest,
    testing::Values(absl::string_view(""),
                    absl::string_view("envoy.load_balancing_policies.maglev"),
                    absl::string_view("envoy.load_balancing_policies.ring_hash"),
                    absl::string_view("envoy.load_balancing_policies.cluster_provided")));

TEST(LoadAwareLocalityConfigTest, FirstSupportedChildWins) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Event::MockDispatcher> dispatcher;
  ON_CALL(context, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher));

  envoy::config::core::v3::TypedExtensionConfig unknown_policy;
  unknown_policy.set_name("envoy.load_balancing_policies.does_not_exist");
  unknown_policy.mutable_typed_config()->PackFrom(Protobuf::Struct{});

  Factory factory;
  auto result =
      factory.loadConfig(context, loadAwareConfig({unknown_policy, roundRobinEndpointPolicy()}));
  ASSERT_TRUE(result.ok());

  const auto& config = typedConfig(result.value());
  EXPECT_EQ("envoy.load_balancing_policies.round_robin", config.endpointPickingPolicyName());
  expectFactoryCreateSucceeds(result.value(), context);
}

TEST(LoadAwareLocalityConfigTest, InvalidWeightUpdatePeriod) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config_proto = loadAwareConfig({roundRobinEndpointPolicy()});
  config_proto.mutable_weight_update_period()->set_seconds(0);

  Factory factory;
  auto result = factory.loadConfig(context, config_proto);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("weight_update_period"));
}

TEST(LoadAwareLocalityConfigTest, MalformedSupportedChildConfigIsRejected) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  envoy::config::core::v3::TypedExtensionConfig malformed_round_robin;
  malformed_round_robin.set_name("envoy.load_balancing_policies.round_robin");
  malformed_round_robin.mutable_typed_config()->set_type_url(
      "type.googleapis.com/"
      "envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin");
  malformed_round_robin.mutable_typed_config()->set_value("not-a-valid-proto");

  Factory factory;
  auto result = factory.loadConfig(context, loadAwareConfig({malformed_round_robin}));
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().code(),
              testing::AnyOf(absl::StatusCode::kInvalidArgument, absl::StatusCode::kInternal));
  EXPECT_THAT(result.status().message(),
              testing::AnyOf(testing::HasSubstr("round_robin.v3.RoundRobin"),
                             testing::HasSubstr("Unable to unpack")));
}

TEST(LoadAwareLocalityConfigTest, NegativeWeightExpirationRejected) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config_proto = loadAwareConfig({roundRobinEndpointPolicy()});
  config_proto.mutable_weight_expiration_period()->set_seconds(-1);

  Factory factory;
  EXPECT_THROW(
      { auto result = factory.loadConfig(context, config_proto); }, ProtoValidationException);
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
