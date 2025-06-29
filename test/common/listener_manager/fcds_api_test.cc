#include <memory>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/listener_manager/fcds_api.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Throw;

namespace Envoy {
namespace Server {
namespace {

class FcdsApiTest : public testing::Test {
public:
  FcdsApiTest() {
    ON_CALL(init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle(init_handle_name_);
    }));
  }

  void setup() {
    envoy::config::core::v3::ConfigSource fcds_config;
    EXPECT_CALL(init_manager_, add(_));
    fcds_ = std::make_unique<FcdsApiImpl>(fcds_config, listener_resource_name_, listener_name_,
                                          cluster_manager_, *store_.rootScope(), init_manager_,
                                          listener_manager_, validation_visitor_);
    EXPECT_CALL(*cluster_manager_.subscription_factory_.subscription_, start(_));
    // Expect the init_watcher_.ready() call that happens during initialization
    EXPECT_CALL(init_watcher_, ready());
    init_target_handle_->initialize(init_watcher_);
    fcds_callbacks_ = cluster_manager_.subscription_factory_.callbacks_;
    subscription_init_target_handle_ =
        fcds_->initTarget().createHandle(subscription_init_handle_name_);
  }

  void expectSubscriptionInitTargetReady() {
    EXPECT_CALL(subscription_init_watcher_, ready());
    subscription_init_target_handle_->initialize(subscription_init_watcher_);
  }

  void expectFilterChainUpdateSuccess(
      size_t expected_added_count,
      const absl::flat_hash_set<absl::string_view>& removed_filter_chains = {}) {
    expectFilterChainUpdate(expected_added_count, removed_filter_chains, absl::OkStatus());
  }

  void expectFilterChainUpdateFailure(
      size_t expected_added_count, const std::string& error_message,
      const absl::flat_hash_set<absl::string_view>& removed_filter_chains = {}) {
    expectFilterChainUpdate(expected_added_count, removed_filter_chains,
                            absl::InvalidArgumentError(error_message));
  }

  void expectFilterChainUpdate(size_t expected_added_count,
                               const absl::flat_hash_set<absl::string_view>& removed_filter_chains,
                               absl::Status result) {
    expectUpdate(listener_name_, expected_added_count, removed_filter_chains, result);
  }

  void expectUpdate(const std::string& listener_name, size_t expected_added_count,
                    const absl::flat_hash_set<absl::string_view>& removed_filter_chains,
                    absl::Status result) {
    EXPECT_CALL(listener_manager_,
                updateDynamicFilterChains(listener_name, _, _, removed_filter_chains))
        .WillOnce(Invoke([result, expected_added_count](
                             const std::string&, absl::optional<std::string>&,
                             const Server::FilterChainRefVector& added,
                             const absl::flat_hash_set<absl::string_view>&) -> absl::Status {
          EXPECT_EQ(expected_added_count, added.size());
          return result;
        }));
  }

  absl::Status
  startConfigUpdate(const std::vector<envoy::config::listener::v3::FilterChain>& filter_chains,
                    const std::vector<std::string>& removed_resources, const std::string& version) {
    const auto decoded_resources = TestUtility::decodeResources(filter_chains);
    Protobuf::RepeatedPtrField<std::string> removed_proto;
    for (const auto& removed : removed_resources) {
      *removed_proto.Add() = removed;
    }
    return fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_proto, version);
  }

  std::string buildFcdsErrorMessage(const std::string& error) {
    return fmt::format("Error updating listener {} with FCDS: {}", listener_name_, error);
  }

  envoy::config::listener::v3::FilterChain buildFilterChain(const std::string& filter_chain_name) {
    envoy::config::listener::v3::FilterChain filter_chain;
    filter_chain.set_name(filter_chain_name);
    return filter_chain;
  }

  const std::string listener_name_ = "test_listener";
  const std::string listener_resource_name_ = "xdstp://test/listener/test_listener";
  const std::string init_handle_name_ = "test";
  const std::string subscription_init_handle_name_ = "subscription_test";
  const std::string filter_chain_1_name_ = "filter-chain-1";
  const std::string filter_chain_2_name_ = "filter-chain-2";
  const std::string filter_chain_3_name_ = "filter-chain-3";
  const std::string invalid_filter_chain_name_ = "invalid-filter-chain";
  const std::string valid_filter_chain_1_name_ = "valid-filter-chain-1";
  const std::string version_0_ = "0";
  const std::string version_1_ = "1";
  const std::string version_2_ = "2";
  const std::string empty_version_ = "";
  const std::string something_wrong_error_ = "something is wrong";
  const std::string envoy_exception_error_ = "envoy exception occurred";
  const std::string sotw_not_implemented_error_ = "SoTW FCDS is not implemented";

  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::MockManager init_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Init::ExpectableWatcherImpl subscription_init_watcher_;
  Init::TargetHandlePtr subscription_init_target_handle_;
  Stats::IsolatedStoreImpl store_;
  MockListenerManager listener_manager_;
  Config::SubscriptionCallbacks* fcds_callbacks_{};
  std::unique_ptr<FcdsApiImpl> fcds_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// Test basic functionality: add, remove, and version updates
TEST_F(FcdsApiTest, BasicFilterChainOperations) {
  InSequence s;

  setup();

  const auto filter_chain_1 = buildFilterChain(filter_chain_1_name_);
  const auto filter_chain_2 = buildFilterChain(filter_chain_2_name_);

  // Test adding multiple filter chains
  expectFilterChainUpdateSuccess(2);
  EXPECT_TRUE(startConfigUpdate({filter_chain_1, filter_chain_2}, {}, version_0_).ok());
  EXPECT_EQ(version_0_, fcds_->versionInfo());

  // Test update with removal and addition
  const auto filter_chain_3 = buildFilterChain(filter_chain_3_name_);
  absl::flat_hash_set<absl::string_view> removed_filter_chains;
  removed_filter_chains.insert(absl::string_view(filter_chain_1_name_));
  expectFilterChainUpdateSuccess(1, removed_filter_chains);
  EXPECT_TRUE(startConfigUpdate({filter_chain_3}, {filter_chain_1_name_}, version_1_).ok());
  EXPECT_EQ(version_1_, fcds_->versionInfo());

  // Test removing all filter chains
  absl::flat_hash_set<absl::string_view> all_removed;
  all_removed.insert(absl::string_view(filter_chain_2_name_));
  all_removed.insert(absl::string_view(filter_chain_3_name_));
  expectFilterChainUpdateSuccess(0, all_removed);
  EXPECT_TRUE(startConfigUpdate({}, {filter_chain_2_name_, filter_chain_3_name_}, version_2_).ok());
  EXPECT_EQ(version_2_, fcds_->versionInfo());
}

TEST_F(FcdsApiTest, InitialStateAndsubscriptionInitTarget) {
  setup();

  // Test initial version is empty
  EXPECT_EQ(empty_version_, fcds_->versionInfo());

  // Test that we can get the subscription init target
  Init::Target& subscription_target = fcds_->initTarget();

  // Verify we can create a handle from it
  auto handle = subscription_target.createHandle("test_subscription");
  EXPECT_NE(handle, nullptr);
}

// Test error handling: absl::Status errors, EnvoyExceptions, and version persistence on failure
TEST_F(FcdsApiTest, ErrorHandlingAndVersionPersistence) {
  InSequence s;

  setup();

  const auto valid_filter_chain = buildFilterChain(filter_chain_1_name_);
  const auto invalid_filter_chain = buildFilterChain(invalid_filter_chain_name_);

  // Test first successful update
  expectFilterChainUpdateSuccess(1);
  expectSubscriptionInitTargetReady();
  EXPECT_TRUE(startConfigUpdate({valid_filter_chain}, {}, version_0_).ok());
  EXPECT_EQ(version_0_, fcds_->versionInfo());

  // Reset subscription init target for next test
  subscription_init_target_handle_ = fcds_->initTarget().createHandle("subscription_test2");

  // Test absl::Status error - version should not change
  expectFilterChainUpdateFailure(1, something_wrong_error_);
  expectSubscriptionInitTargetReady();
  const auto result1 = startConfigUpdate({invalid_filter_chain}, {}, version_1_);
  EXPECT_FALSE(result1.ok());
  EXPECT_EQ(result1.message(), buildFcdsErrorMessage(something_wrong_error_));
  EXPECT_EQ(version_0_, fcds_->versionInfo()); // Version should remain unchanged

  // Reset subscription init target for EnvoyException test
  subscription_init_target_handle_ = fcds_->initTarget().createHandle("subscription_test3");

  // Test EnvoyException handling
  EXPECT_CALL(listener_manager_, updateDynamicFilterChains(_, _, _, _))
      .WillOnce(Throw(EnvoyException(envoy_exception_error_)));
  expectSubscriptionInitTargetReady();

  const auto result2 = startConfigUpdate({invalid_filter_chain}, {}, version_1_);
  EXPECT_EQ(result2.message(), buildFcdsErrorMessage(envoy_exception_error_));
  EXPECT_EQ(version_0_, fcds_->versionInfo()); // Version should still remain unchanged
}

// Test subscription-level failures and different failure reasons
TEST_F(FcdsApiTest, SubscriptionFailuresAndReasons) {
  setup();

  // Test FetchTimedout
  expectSubscriptionInitTargetReady();
  fcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                        nullptr);
  EXPECT_EQ(empty_version_, fcds_->versionInfo());

  // Reset for next test
  subscription_init_target_handle_ = fcds_->initTarget().createHandle("subscription_test2");

  // Test UpdateRejected
  expectSubscriptionInitTargetReady();
  fcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                        nullptr);
  EXPECT_EQ(empty_version_, fcds_->versionInfo());
}

TEST_F(FcdsApiTest, SotwNotImplemented) {
  setup();

  const auto filter_chain = buildFilterChain(filter_chain_1_name_);
  const auto decoded_resources = TestUtility::decodeResources({filter_chain});

  EXPECT_EQ(fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, version_1_).message(),
            sotw_not_implemented_error_);
}

TEST_F(FcdsApiTest, FailureStatePropagation) {
  InSequence s;

  setup();

  const auto filter_chain = buildFilterChain(filter_chain_1_name_);

  // Test failure state propagation
  EXPECT_CALL(listener_manager_, updateDynamicFilterChains(_, _, _, _))
      .WillOnce(::testing::Return(absl::InvalidArgumentError(something_wrong_error_)));
  expectSubscriptionInitTargetReady();

  const auto result = startConfigUpdate({filter_chain}, {}, version_1_);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.message(), buildFcdsErrorMessage(something_wrong_error_));

  // Reset for success state test
  subscription_init_target_handle_ = fcds_->initTarget().createHandle("subscription_test2");

  // Test success state propagation (null failure state)
  EXPECT_CALL(listener_manager_, updateDynamicFilterChains(_, _, _, _))
      .WillOnce(::testing::Return(absl::OkStatus()));
  expectSubscriptionInitTargetReady();

  const auto result2 = startConfigUpdate({filter_chain}, {}, version_1_);
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(version_1_, fcds_->versionInfo());
}

TEST_F(FcdsApiTest, ConsecutiveVersionUpdates) {
  InSequence s;

  setup();

  const auto filter_chain_1 = buildFilterChain(filter_chain_1_name_);
  const auto filter_chain_2 = buildFilterChain(filter_chain_2_name_);
  const auto filter_chain_3 = buildFilterChain(filter_chain_3_name_);

  // First update
  expectFilterChainUpdateSuccess(1);
  EXPECT_TRUE(startConfigUpdate({filter_chain_1}, {}, version_0_).ok());
  EXPECT_EQ(version_0_, fcds_->versionInfo());

  // Second update with new version
  expectFilterChainUpdateSuccess(1);
  EXPECT_TRUE(startConfigUpdate({filter_chain_2}, {}, version_1_).ok());
  EXPECT_EQ(version_1_, fcds_->versionInfo());

  // Third update with another new version
  expectFilterChainUpdateSuccess(1);
  EXPECT_TRUE(startConfigUpdate({filter_chain_3}, {}, version_2_).ok());
  EXPECT_EQ(version_2_, fcds_->versionInfo());
}

} // namespace
} // namespace Server
} // namespace Envoy
