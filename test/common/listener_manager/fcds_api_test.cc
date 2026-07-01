#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/listener_manager/fcds_api.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::HasStatus;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace Envoy {
namespace Server {
namespace {

class MockFilterChainUpdateCallbacks : public FilterChainUpdateCallbacks {
public:
  MOCK_METHOD(absl::Status, onFilterChainUpdated, (const FilterChainProto& proto), (override));
  MOCK_METHOD(void, onFilterChainRemoved, (Network::DrainableFilterChainSharedPtr && draining),
              (override));
};

class FcdsApiTest : public testing::Test {
public:
  FcdsApiTest() {
    ON_CALL(init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
    ON_CALL(init_manager_, initialize(_))
        .WillByDefault(Invoke(
            [this](const Init::Watcher& watcher) { init_target_handle_->initialize(watcher); }));
  }

  void setup(const std::string& filter_chain_name) {
    envoy::config::listener::v3::Listener::FcdsConfig fcds_config;
    fcds_config.mutable_config_source()->set_resource_api_version(
        envoy::config::core::v3::ApiVersion::V3);

    auto subscription = std::make_unique<NiceMock<Config::MockSubscription>>();
    auto* raw_subscription = subscription.get();
    EXPECT_CALL(*raw_subscription, start(testing::ElementsAre(filter_chain_name)));

    EXPECT_CALL(cluster_manager_.subscription_factory_,
                subscriptionFromConfigSource(_, _, _, _, _, _))
        .WillOnce(Invoke([this, subscription = std::move(subscription)](
                             const envoy::config::core::v3::ConfigSource&, absl::string_view,
                             Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                             Config::OpaqueResourceDecoderSharedPtr,
                             const Config::SubscriptionOptions&) mutable
                         -> absl::StatusOr<Config::SubscriptionPtr> {
          fcds_callbacks_ = &callbacks;
          return std::move(subscription);
        }));

    absl::Status creation_status;
    fcds_api_ = std::make_unique<FcdsApiImpl>(fcds_config.config_source(), filter_chain_name,
                                              callbacks_, cluster_manager_, scope_,
                                              validation_visitor_, creation_status);
    EXPECT_TRUE(creation_status.ok());
    init_manager_.add(fcds_api_->initTarget());
    init_manager_.initialize(init_watcher_);
  }

  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& scope_{*stats_store_.rootScope()};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<MockFilterChainUpdateCallbacks> callbacks_;
  Config::SubscriptionCallbacks* fcds_callbacks_{};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Init::MockManager> init_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  // Should be last to exercise the readiness triggers.
  std::unique_ptr<FcdsApiImpl> fcds_api_;
};

TEST_F(FcdsApiTest, OnConfigUpdateDecodesAndPropagates) {
  setup("chain-1");

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain-1");
  auto* filter = filter_chain.add_filters();
  filter->set_name("http");

  // Verify that config update correctly decodes the resources and calls callbacks with proto config
  EXPECT_CALL(callbacks_, onFilterChainUpdated(testing::Property(
                              &envoy::config::listener::v3::FilterChain::name, "chain-1")))
      .WillOnce(Return(absl::OkStatus()));

  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_EQ(fcds_api_->filterChain(), nullptr);
  EXPECT_TRUE(
      fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "v1").ok());
  EXPECT_EQ(fcds_api_->versionInfo(), "v1");
  // Still warming.
  EXPECT_EQ(fcds_api_->filterChain(), nullptr);

  // Ensure the idempotent update does not trigger a callback.
  EXPECT_TRUE(
      fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "v1").ok());

  // Destruction unblocks the watcher.
  EXPECT_CALL(init_watcher_, ready());
}

TEST_F(FcdsApiTest, OnConfigUpdateRemoved) {
  setup("chain-1");

  auto mock_filter_chain = std::make_shared<NiceMock<Network::MockFilterChain>>();
  EXPECT_CALL(init_watcher_, ready());
  fcds_api_->setFilterChain(Network::DrainableFilterChainSharedPtr(mock_filter_chain));
  EXPECT_NE(fcds_api_->filterChain(), nullptr);

  EXPECT_CALL(callbacks_, onFilterChainRemoved(testing::Eq(mock_filter_chain)));

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("chain-1");

  EXPECT_TRUE(fcds_callbacks_->onConfigUpdate({}, removed_resources, "v2").ok());
  EXPECT_EQ(fcds_api_->versionInfo(), "v2");
  EXPECT_EQ(fcds_api_->filterChain(), nullptr);
}

TEST_F(FcdsApiTest, OnConfigUpdateSotw) {
  setup("chain-1");

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain-1");
  auto* filter = filter_chain.add_filters();
  filter->set_name("http");

  // Verify that config update correctly decodes the resources and calls callbacks with proto config
  EXPECT_CALL(callbacks_, onFilterChainUpdated(testing::Property(
                              &envoy::config::listener::v3::FilterChain::name, "chain-1")))
      .WillOnce(Return(absl::OkStatus()));

  const auto decoded_resources = TestUtility::decodeResources({filter_chain});

  EXPECT_TRUE(fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "v1").ok());
  EXPECT_EQ(fcds_api_->versionInfo(), "v1");
  // Destruction unblocks the watcher.
  EXPECT_CALL(init_watcher_, ready());
}

TEST_F(FcdsApiTest, OnConfigUpdateRemovedSotw) {
  setup("chain-1");

  EXPECT_EQ(fcds_api_->filterChain(), nullptr);
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_TRUE(fcds_callbacks_->onConfigUpdate({}, "v2").ok());
  EXPECT_EQ(fcds_api_->versionInfo(), "v2");
  EXPECT_EQ(fcds_api_->filterChain(), nullptr);
}

TEST_F(FcdsApiTest, ErrorAddRemove) {
  setup("chain-1");

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain-1");
  auto* filter = filter_chain.add_filters();
  filter->set_name("http");
  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("chain-2");

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THAT(fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "v1"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("cannot add and remove")));
}

TEST_F(FcdsApiTest, ErrorAddBadName) {
  setup("chain-1");

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain-2");
  auto* filter = filter_chain.add_filters();
  filter->set_name("http");
  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THAT(
      fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "v1"),
      HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("invalid filter chain name")));
}

TEST_F(FcdsApiTest, ErrorBadCounts) {
  setup("chain-1");

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("chain-1");
  removed_resources.Add("chain-2");

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THAT(
      fcds_callbacks_->onConfigUpdate({}, removed_resources, "v1"),
      HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("must remove exactly one resource")));
}

TEST_F(FcdsApiTest, ErrorRemoveBadName) {
  setup("chain-1");

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("chain-2");

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THAT(fcds_callbacks_->onConfigUpdate({}, removed_resources, "v1"),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("invalid removed filter chain name")));
}

} // namespace
} // namespace Server
} // namespace Envoy
