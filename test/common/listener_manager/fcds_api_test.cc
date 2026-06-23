#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/listener_manager/fcds_api.h"
#include "source/common/listener_manager/filter_chain_manager_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/listener_component_factory.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace Envoy {
namespace Server {
namespace {

class MockFilterChainUpdateCallbacks : public FilterChainUpdateCallbacks {
public:
  MOCK_METHOD(absl::Status, onFilterChainUpdate,
              (const std::vector<Network::DrainableFilterChainSharedPtr>& added_or_updated,
               const std::vector<std::string>& removed),
              (override));
};

class MockFcdsSharedFilterChainManager : public FcdsSharedFilterChainManager {
public:
  MockFcdsSharedFilterChainManager(Server::Configuration::ServerFactoryContext& server_context,
                                   ListenerComponentFactory& listener_component_factory)
      : FcdsSharedFilterChainManager(server_context, listener_component_factory) {}

  MOCK_METHOD(absl::StatusOr<Network::DrainableFilterChainSharedPtr>,
              createOrUpdateSharedFilterChain,
              (const envoy::config::listener::v3::FilterChain& config), (override));
  MOCK_METHOD(Network::DrainableFilterChainSharedPtr, findSharedFilterChain,
              (const std::string& name), (const, override));
};

class FcdsApiTest : public testing::Test {
public:
  FcdsApiTest()
      : shared_manager_(server_context_, listener_component_factory_),
        init_target_("test", []() {}) {
    init_target_handle_ = init_target_.createHandle("test");
    init_target_handle_->initialize(init_watcher_);
  }

  void setup(const std::string& filter_chain_name) {
    envoy::config::listener::v3::Listener::FcdsConfig fcds_config;
    fcds_config.mutable_config_source()->set_resource_api_version(
        envoy::config::core::v3::ApiVersion::V3);

    fcds_api_ = std::make_unique<FcdsApiImpl>(fcds_config, filter_chain_name, shared_manager_,
                                              cluster_manager_, server_context_.scope(),
                                              validation_visitor_);
  }

  void startSubscription(const std::string& filter_chain_name) {
    auto subscription = std::make_unique<NiceMock<Config::MockSubscription>>();
    auto* raw_subscription = subscription.get();
    EXPECT_CALL(*raw_subscription, start(testing::ElementsAre(filter_chain_name)));

    EXPECT_CALL(cluster_manager_.subscription_factory_, subscriptionFromConfigSource(_, _, _, _, _, _))
        .WillOnce(Invoke([this, subscription = std::move(subscription)](
                             const envoy::config::core::v3::ConfigSource&, absl::string_view,
                             Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                             Config::OpaqueResourceDecoderSharedPtr,
                             const Config::SubscriptionOptions&) mutable -> absl::StatusOr<Config::SubscriptionPtr> {
          fcds_callbacks_ = &callbacks;
          return std::move(subscription);
        }));

    EXPECT_TRUE(fcds_api_->subscribeClient(callbacks_, init_target_).ok());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<MockListenerComponentFactory> listener_component_factory_;
  MockFcdsSharedFilterChainManager shared_manager_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<MockFilterChainUpdateCallbacks> callbacks_;
  Init::TargetImpl init_target_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Config::SubscriptionCallbacks* fcds_callbacks_{};
  std::unique_ptr<FcdsApiImpl> fcds_api_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

TEST_F(FcdsApiTest, SubscribeToFilterChainName) {
  setup("chain-1");
  startSubscription("chain-1");
}

TEST_F(FcdsApiTest, OnConfigUpdateDecodesAndPropagates) {
  setup("chain-1");
  startSubscription("chain-1");

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain-1");
  auto* filter = filter_chain.add_filters();
  filter->set_name("http");

  auto mock_filter_chain = std::make_shared<NiceMock<Network::MockFilterChain>>();

  // Mock successful compilation in the shared manager when config update is received
  EXPECT_CALL(shared_manager_, createOrUpdateSharedFilterChain(testing::Property(
                                   &envoy::config::listener::v3::FilterChain::name, "chain-1")))
      .WillOnce(Return(mock_filter_chain));

  // Verify that config update correctly decodes the resources and calls callbacks with compiled
  // object
  EXPECT_CALL(callbacks_, onFilterChainUpdate(testing::ElementsAre(mock_filter_chain),
                                              testing::ElementsAre("chain-2")))
      .WillOnce(Return(absl::Status(absl::OkStatus())));
  EXPECT_CALL(init_watcher_, ready());

  const auto decoded_resources = TestUtility::decodeResources({filter_chain});

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("chain-2");

  EXPECT_TRUE(
      fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "v1").ok());
  EXPECT_EQ(fcds_api_->versionInfo(), "v1");
  EXPECT_EQ(fcds_api_->filterChain(), mock_filter_chain);
}

} // namespace
} // namespace Server
} // namespace Envoy
