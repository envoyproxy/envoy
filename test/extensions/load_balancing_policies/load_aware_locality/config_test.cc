#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/typed_load_balancer_factory.h"
#include "test/test_common/registry.h"

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
  std::ignore = config.mutable_typed_config()->PackFrom(message);
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

  Factory factory;
  auto lb = factory.create(*config, cluster_info, priority_set, context.runtime_loader_,
                           context.api_.random_, context.time_system_);
  ASSERT_NE(nullptr, lb);
  ASSERT_TRUE(lb->initialize().ok());

  auto worker_factory = lb->factory();
  ASSERT_NE(nullptr, worker_factory);
  EXPECT_NE(nullptr, worker_factory->create({priority_set, nullptr}));
}

class FailingLoadConfigFactory : public Upstream::MockTypedLoadBalancerFactory {
public:
  std::string name() const override {
    return "envoy.load_balancing_policies.load_aware_locality_test_error";
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&, const Protobuf::Message&) override {
    return absl::InvalidArgumentError("failing child loadConfig");
  }
};

class RejectingEndpointValidationConfig : public Upstream::LoadBalancerConfig {
public:
  absl::Status validateEndpoints(const Upstream::PriorityState&) const override {
    return absl::InvalidArgumentError("child endpoint validation failed");
  }
};

TEST(LoadAwareLocalityConfigTest, Defaults) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  Factory factory;
  auto result = factory.loadConfig(context, loadAwareConfig({roundRobinEndpointPolicy()}));
  ASSERT_TRUE(result.ok());

  const auto& config = typedConfig(result.value());
  EXPECT_EQ(std::chrono::milliseconds(1000), config.weightUpdatePeriod());
  EXPECT_DOUBLE_EQ(0.1, config.utilizationVarianceThreshold());
  // Default alpha derived from weight_update_period=1s and smoothing_time_constant=5s.
  EXPECT_DOUBLE_EQ(1.0 - std::exp(-1.0 / 5.0), config.ewmaAlpha());
  EXPECT_DOUBLE_EQ(0.03, config.remoteProbeFraction());
  EXPECT_EQ(std::chrono::milliseconds(180000), config.weightExpirationPeriod());

  expectFactoryCreateSucceeds(result.value(), context);
}

TEST(LoadAwareLocalityConfigTest, Overrides) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config_proto = loadAwareConfig({roundRobinEndpointPolicy()});
  config_proto.mutable_weight_update_period()->set_seconds(2);
  config_proto.mutable_utilization_variance_threshold()->set_value(0.05);
  config_proto.mutable_smoothing_time_constant()->set_seconds(4);
  config_proto.mutable_remote_probe_fraction()->set_value(0.07);
  config_proto.mutable_weight_expiration_period()->set_seconds(15);

  Factory factory;
  auto result = factory.loadConfig(context, config_proto);
  ASSERT_TRUE(result.ok());

  const auto& config = typedConfig(result.value());
  EXPECT_EQ(std::chrono::milliseconds(2000), config.weightUpdatePeriod());
  EXPECT_DOUBLE_EQ(0.05, config.utilizationVarianceThreshold());
  // Alpha derived from weight_update_period=2s and smoothing_time_constant=4s.
  EXPECT_DOUBLE_EQ(1.0 - std::exp(-2.0 / 4.0), config.ewmaAlpha());
  EXPECT_DOUBLE_EQ(0.07, config.remoteProbeFraction());
  EXPECT_EQ(std::chrono::milliseconds(15000), config.weightExpirationPeriod());

  expectFactoryCreateSucceeds(result.value(), context);
}

TEST(LoadAwareLocalityConfigTest, MissingChildPolicy) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  Factory factory;
  auto result = factory.loadConfig(context, loadAwareConfig({}));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status(), absl::InvalidArgumentError("No supported endpoint picking policy."));
}

TEST(LoadAwareLocalityConfigTest, SelfNestedChildPolicyRejected) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  Factory factory;
  auto result = factory.loadConfig(
      context,
      loadAwareConfig({typedExtensionConfig("envoy.load_balancing_policies.load_aware_locality",
                                            loadAwareConfig({roundRobinEndpointPolicy()}))}));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("its own endpoint_picking_policy"));
}

TEST(LoadAwareLocalityConfigTest, FirstSupportedChildWins) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  envoy::config::core::v3::TypedExtensionConfig unknown_policy;
  unknown_policy.set_name("envoy.load_balancing_policies.does_not_exist");
  std::ignore = unknown_policy.mutable_typed_config()->PackFrom(Protobuf::Struct{});

  Factory factory;
  auto result =
      factory.loadConfig(context, loadAwareConfig({unknown_policy, roundRobinEndpointPolicy()}));
  ASSERT_TRUE(result.ok());

  const auto& config = typedConfig(result.value());
  EXPECT_EQ("envoy.load_balancing_policies.round_robin", config.endpointPickingPolicyName());
  expectFactoryCreateSucceeds(result.value(), context);
}

TEST(LoadAwareLocalityConfigTest, InvalidPolicyKnobsRejected) {
  struct Case {
    std::string name;
    std::function<void(LoadAwareLocalityLbProto&)> mutate;
    std::string message;
    bool throws_validation_exception{};
  };

  const std::vector<Case> cases = {
      {"ZeroWeightUpdatePeriod",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_weight_update_period()->set_seconds(0);
       },
       "weight_update_period"},
      {"WeightUpdatePeriodBelowFloor",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_weight_update_period()->set_nanos(50000000);
       },
       "at least 100ms"},
      {"ZeroSmoothingTimeConstant",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_smoothing_time_constant()->set_seconds(0);
       },
       "smoothing_time_constant"},
      {"VarianceThresholdBelowRange",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_utilization_variance_threshold()->set_value(-0.1);
       },
       "utilization_variance_threshold"},
      {"VarianceThresholdAboveRange",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_utilization_variance_threshold()->set_value(1.5);
       },
       "utilization_variance_threshold"},
      {"RemoteProbeFractionAboveRange",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_remote_probe_fraction()->set_value(1.0);
       },
       "remote_probe_fraction"},
      {"RemoteProbeFractionBelowRange",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_remote_probe_fraction()->set_value(-0.01);
       },
       "remote_probe_fraction"},
      {"NegativeWeightExpiration",
       [](LoadAwareLocalityLbProto& config) {
         config.mutable_weight_expiration_period()->set_seconds(-1);
       },
       "", true},
  };

  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    auto config_proto = loadAwareConfig({roundRobinEndpointPolicy()});
    c.mutate(config_proto);

    Factory factory;
    if (c.throws_validation_exception) {
      EXPECT_THROW(
          { auto result = factory.loadConfig(context, config_proto); }, ProtoValidationException);
      continue;
    }

    auto result = factory.loadConfig(context, config_proto);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(result.status().message(), testing::HasSubstr(c.message));
  }
}

TEST(LoadAwareLocalityConfigTest, OobFieldsAreAcceptedAsNoOp) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // Out-of-band reporting is not implemented yet; the fields must be accepted so configs can be
  // rolled out ahead of the implementation.
  auto config_proto = loadAwareConfig({roundRobinEndpointPolicy()});
  config_proto.mutable_enable_oob_load_report()->set_value(true);
  config_proto.mutable_oob_reporting_period()->set_seconds(10);

  Factory factory;
  EXPECT_TRUE(factory.loadConfig(context, config_proto).ok());
}

TEST(LoadAwareLocalityConfigTest, PolicyValidationPrecedesChildResolution) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // With an unresolvable child AND an invalid policy knob, the policy-knob error must win so the
  // user isn't sent chasing the wrong problem.
  auto config_proto = loadAwareConfig({});
  config_proto.mutable_weight_update_period()->set_nanos(50000000);

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

TEST(LoadAwareLocalityConfigTest, SupportedChildLoadConfigErrorIsPropagated) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  FailingLoadConfigFactory child_factory;
  Registry::InjectFactory<Upstream::TypedLoadBalancerFactory> registered_factory(child_factory);

  Factory factory;
  auto result = factory.loadConfig(
      context, loadAwareConfig({typedExtensionConfig(child_factory.name(), Protobuf::Struct{})}));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("failing child loadConfig"));
}

TEST(LoadAwareLocalityConfigTest, EndpointValidationDelegatesToChildConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockTypedLoadBalancerFactory> child_factory;
  NiceMock<Event::MockDispatcher> dispatcher;

  LoadAwareLocalityLbConfig config(
      child_factory, std::make_shared<RejectingEndpointValidationConfig>(),
      std::chrono::milliseconds(1000), 0.1, 0.3, 0.03, std::chrono::milliseconds(180000),
      std::vector<std::string>{}, dispatcher, context.thread_local_);

  const Upstream::PriorityState priorities;
  auto status = config.validateEndpoints(priorities);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), testing::HasSubstr("child endpoint validation failed"));
}

TEST(LoadAwareLocalityConfigTest, MetricNamesReachConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config_proto = loadAwareConfig({roundRobinEndpointPolicy()});
  config_proto.add_metric_names_for_computing_utilization("named_metrics.foo");
  config_proto.add_metric_names_for_computing_utilization("named_metrics.bar");

  Factory factory;
  auto result = factory.loadConfig(context, config_proto);
  ASSERT_TRUE(result.ok());

  const auto& config = typedConfig(result.value());
  ASSERT_EQ(2u, config.metricNamesForComputingUtilization().size());
  EXPECT_EQ("named_metrics.foo", config.metricNamesForComputingUtilization()[0]);
  EXPECT_EQ("named_metrics.bar", config.metricNamesForComputingUtilization()[1]);

  expectFactoryCreateSucceeds(result.value(), context);
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
