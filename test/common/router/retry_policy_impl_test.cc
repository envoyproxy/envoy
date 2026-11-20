#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/retry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/router/retry_policy_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::Envoy::StatusHelpers::StatusIs;
using ::testing::HasSubstr;
using ::testing::NiceMock;

class CustomTestRetryPolicyFactory : public RetryPolicyFactory {
public:
  absl::StatusOr<RetryPolicyConstSharedPtr>
  createRetryPolicy(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&,
                    Server::Configuration::CommonFactoryContext&) override {
    auto policy = std::make_shared<TestRetryPolicy>();
    policy->num_retries_ = 7;
    return policy;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "envoy.router.retry_policy.custom_test"; }
};

class RetryPolicyFactoryTest : public testing::Test {
protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
};

TEST_F(RetryPolicyFactoryTest, DefaultFactoryRegistrationAndMetadata) {
  auto* factory = Registry::FactoryRegistry<RetryPolicyFactory>::getFactory(
      "envoy.router.retry_policy.default");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory, Registry::FactoryRegistry<RetryPolicyFactory>::getFactoryByType(
                         "envoy.config.route.v3.RetryPolicy"));
  EXPECT_EQ("envoy.router.retry_policy.default", factory->name());
  EXPECT_EQ("envoy.router.retry_policy", factory->category());

  ProtobufTypes::MessagePtr empty_proto = factory->createEmptyConfigProto();
  ASSERT_NE(empty_proto, nullptr);
  EXPECT_NE(Protobuf::DynamicCastMessage<envoy::config::route::v3::RetryPolicy>(empty_proto.get()),
            nullptr);
}

TEST_F(RetryPolicyFactoryTest, DefaultFactoryCreatesValidRetryPolicy) {
  auto* factory = Registry::FactoryRegistry<RetryPolicyFactory>::getFactory(
      "envoy.router.retry_policy.default");
  ASSERT_NE(factory, nullptr);

  envoy::config::route::v3::RetryPolicy retry_policy_proto;
  TestUtility::loadFromYaml(R"EOF(
retry_on: "5xx,connect-failure"
num_retries: 3
per_try_timeout: 0.25s
refresh_cluster_on_retry: true
)EOF",
                            retry_policy_proto);

  Protobuf::Any typed_config;
  ASSERT_TRUE(typed_config.PackFrom(retry_policy_proto));

  ProtobufTypes::MessagePtr translated = Envoy::Config::Utility::translateAnyToFactoryConfig(
      typed_config, ProtobufMessage::getStrictValidationVisitor(), *factory);
  ASSERT_NE(translated, nullptr);

  absl::StatusOr<RetryPolicyConstSharedPtr> policy_or_error = factory->createRetryPolicy(
      *translated, ProtobufMessage::getStrictValidationVisitor(), factory_context_);
  ASSERT_TRUE(policy_or_error.ok()) << policy_or_error.status();
  ASSERT_NE(*policy_or_error, nullptr);

  EXPECT_EQ(3u, (*policy_or_error)->numRetries());
  EXPECT_EQ(std::chrono::milliseconds(250), (*policy_or_error)->perTryTimeout());
  EXPECT_EQ(RetryPolicy::RETRY_ON_5XX | RetryPolicy::RETRY_ON_CONNECT_FAILURE,
            (*policy_or_error)->retryOn());
  EXPECT_TRUE((*policy_or_error)->refreshClusterOnRetry());
}

TEST_F(RetryPolicyFactoryTest, DefaultFactoryReturnsErrorOnInvalidBackoffIntervals) {
  auto* factory = Registry::FactoryRegistry<RetryPolicyFactory>::getFactory(
      "envoy.router.retry_policy.default");
  ASSERT_NE(factory, nullptr);

  envoy::config::route::v3::RetryPolicy retry_policy_proto;
  TestUtility::loadFromYaml(R"EOF(
retry_on: "5xx"
retry_back_off:
  base_interval: 0.5s
  max_interval: 0.1s
)EOF",
                            retry_policy_proto);

  absl::StatusOr<RetryPolicyConstSharedPtr> policy_or_error = factory->createRetryPolicy(
      retry_policy_proto, ProtobufMessage::getStrictValidationVisitor(), factory_context_);
  EXPECT_THAT(policy_or_error, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      policy_or_error,
      HasStatusMessage(HasSubstr("max_interval must greater than or equal to the base_interval")));
}

TEST_F(RetryPolicyFactoryTest, DefaultFactoryValidatesProtoConstraints) {
  auto* factory = Registry::FactoryRegistry<RetryPolicyFactory>::getFactory(
      "envoy.router.retry_policy.default");
  ASSERT_NE(factory, nullptr);

  envoy::config::route::v3::RetryPolicy retry_policy_proto;
  retry_policy_proto.mutable_retry_back_off()->mutable_base_interval()->set_seconds(0);

  EXPECT_THROW_WITH_REGEX(factory
                              ->createRetryPolicy(retry_policy_proto,
                                                  ProtobufMessage::getStrictValidationVisitor(),
                                                  factory_context_)
                              .IgnoreError(),
                          ProtoValidationException, "Proto constraint validation failed");
}

TEST_F(RetryPolicyFactoryTest, CustomRetryPolicyFactoryRegistration) {
  CustomTestRetryPolicyFactory custom_factory;
  Registry::InjectFactory<RetryPolicyFactory> registered(custom_factory);

  auto* factory = Registry::FactoryRegistry<RetryPolicyFactory>::getFactory(
      "envoy.router.retry_policy.custom_test");
  ASSERT_EQ(&custom_factory, factory);
  EXPECT_EQ("envoy.router.retry_policy", factory->category());

  ProtobufTypes::MessagePtr empty_proto = factory->createEmptyConfigProto();
  ASSERT_NE(empty_proto, nullptr);

  absl::StatusOr<RetryPolicyConstSharedPtr> policy_or_error = factory->createRetryPolicy(
      *empty_proto, ProtobufMessage::getStrictValidationVisitor(), factory_context_);
  ASSERT_TRUE(policy_or_error.ok()) << policy_or_error.status();
  ASSERT_NE(*policy_or_error, nullptr);
  EXPECT_EQ(7u, (*policy_or_error)->numRetries());
}

} // namespace
} // namespace Router
} // namespace Envoy
