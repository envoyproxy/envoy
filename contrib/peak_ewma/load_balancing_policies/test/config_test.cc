#include "envoy/registry/registry.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/load_balancing_policies/peak_ewma/v3alpha/peak_ewma.pb.h"
#include "contrib/peak_ewma/load_balancing_policies/source/config.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

// Simple ThreadLocal mock for testing
class MockThreadLocalInstance : public ThreadLocal::SlotAllocator {
public:
  ThreadLocal::SlotPtr allocateSlot() override { return std::make_unique<MockSlot>(); }

private:
  class MockSlot : public ThreadLocal::Slot {
  public:
    bool currentThreadRegistered() override { return true; }
    ThreadLocal::ThreadLocalObjectSharedPtr get() override { return nullptr; }
    void set(InitializeCb) override {}
    void runOnAllThreads(const UpdateCb&) override {}
    void runOnAllThreads(const UpdateCb&, const std::function<void()>&) override {}
    bool isShutdown() const override { return false; }
  };
};

namespace {

class PeakEwmaConfigTest : public ::testing::Test {
public:
  PeakEwmaConfigTest()
      : stat_names_(store_.symbolTable()), stats_(stat_names_, *store_.rootScope()) {
    ON_CALL(*cluster_info_, statsScope()).WillByDefault(ReturnRef(*store_.rootScope()));

    // Set up mock time source for PeakEwmaLoadBalancer constructor calls
    ON_CALL(time_source_, monotonicTime())
        .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1234567890))));
  }

  Stats::TestUtil::TestStore store_;
  Upstream::ClusterLbStatNames stat_names_;
  Upstream::ClusterLbStats stats_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockTimeSystem> time_source_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  MockThreadLocalInstance tls_;
};

TEST_F(PeakEwmaConfigTest, FactoryRegistration) {
  // Verify that the factory is properly registered
  auto factory = Registry::FactoryRegistry<Upstream::TypedLoadBalancerFactory>::getFactory(
      "envoy.load_balancing_policies.peak_ewma");
  EXPECT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.load_balancing_policies.peak_ewma");
}

TEST_F(PeakEwmaConfigTest, CreateEmptyConfigProto) {
  Factory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);

  // Verify it's the right type
  const auto* typed_proto =
      dynamic_cast<const envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma*>(
          proto.get());
  EXPECT_NE(typed_proto, nullptr);
}

TEST_F(PeakEwmaConfigTest, LoadConfigWithDefaults) {
  Factory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // Create a minimal config proto
  envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma proto_config;

  auto result = factory.loadConfig(context, proto_config);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);

  // Verify the config holds the proto
  const auto* config = dynamic_cast<const TypedPeakEwmaLbConfig*>(result.value().get());
  EXPECT_NE(config, nullptr);
}

TEST_F(PeakEwmaConfigTest, LoadConfigWithCustomValues) {
  Factory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // Create config with custom values
  envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma proto_config;
  proto_config.mutable_decay_time()->set_seconds(5);         // 5 second decay time
  proto_config.mutable_penalty_value()->set_value(750000.0); // Custom penalty value

  auto result = factory.loadConfig(context, proto_config);
  EXPECT_TRUE(result.ok());

  const auto* config = dynamic_cast<const TypedPeakEwmaLbConfig*>(result.value().get());
  EXPECT_NE(config, nullptr);
  EXPECT_EQ(config->lb_config_.decay_time().seconds(), 5);
  EXPECT_TRUE(config->lb_config_.has_penalty_value());
  EXPECT_EQ(config->lb_config_.penalty_value().value(), 750000.0);
}

TEST_F(PeakEwmaConfigTest, CreateThreadAwareLoadBalancer) {
  Factory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // Create config
  envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma proto_config;

  auto config_result = factory.loadConfig(context, proto_config);
  EXPECT_TRUE(config_result.ok());

  // Create the thread-aware load balancer
  auto talb = factory.create(OptRef<const Upstream::LoadBalancerConfig>(*config_result.value()),
                             *cluster_info_, priority_set_, runtime_, random_, time_source_);

  EXPECT_NE(talb, nullptr);

  // Initialize the load balancer
  auto status = talb->initialize();
  EXPECT_TRUE(status.ok());

  // Get the factory and create a load balancer instance
  auto lb_factory = talb->factory();
  EXPECT_NE(lb_factory, nullptr);

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = lb_factory->create(params);
  EXPECT_NE(lb, nullptr);

  // The load balancer should be of the correct type
  const auto* peak_ewma_lb = dynamic_cast<const PeakEwmaLoadBalancer*>(lb.get());
  EXPECT_NE(peak_ewma_lb, nullptr);
}

TEST_F(PeakEwmaConfigTest, CreateWithNullConfig) {
  Factory factory;

  // Should handle null config gracefully and return a valid load balancer
  auto talb = factory.create(OptRef<const Upstream::LoadBalancerConfig>(), *cluster_info_,
                             priority_set_, runtime_, random_, time_source_);

  EXPECT_NE(talb, nullptr);

  // Should be able to initialize successfully
  auto status = talb->initialize();
  EXPECT_TRUE(status.ok());
}

TEST_F(PeakEwmaConfigTest, ConfigValidation) {
  // Test that extreme values are handled
  envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma proto_config;

  // Very small decay time
  proto_config.mutable_decay_time()->set_nanos(1000000); // 1ms

  TypedPeakEwmaLbConfig config(proto_config, dispatcher_);
  EXPECT_EQ(config.lb_config_.decay_time().nanos(), 1000000);

  // Very large decay time
  proto_config.mutable_decay_time()->set_seconds(300);

  TypedPeakEwmaLbConfig config2(proto_config, dispatcher_);
  EXPECT_EQ(config2.lb_config_.decay_time().seconds(), 300);
}

TEST_F(PeakEwmaConfigTest, MultipleLoadBalancerInstances) {
  Factory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma proto_config;

  auto config_result = factory.loadConfig(context, proto_config);
  EXPECT_TRUE(config_result.ok());

  auto talb = factory.create(OptRef<const Upstream::LoadBalancerConfig>(*config_result.value()),
                             *cluster_info_, priority_set_, runtime_, random_, time_source_);

  auto lb_factory = talb->factory();

  // Create multiple load balancer instances from the same factory
  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb1 = lb_factory->create(params);
  auto lb2 = lb_factory->create(params);

  EXPECT_NE(lb1, nullptr);
  EXPECT_NE(lb2, nullptr);
  EXPECT_NE(lb1.get(), lb2.get()); // Should be different instances
}

} // namespace
} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
