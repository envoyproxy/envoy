#include <memory>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "source/common/protobuf/utility.h"
#include "source/common/network/address_impl.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "test/mocks/server/factory_context.h"
#include "source/extensions/listener_managers/listener_manager/filter_chain_manager_impl.h"
#include "source/extensions/listener_managers/listener_manager/lds_api.h"
#include "gmock/gmock.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Throw;

namespace Envoy {
namespace Server {
namespace {

class MockFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  MockFilterChainFactoryBuilder() {
    ON_CALL(*this, buildFilterChain(_, _))
        .WillByDefault(Return(std::make_shared<Network::MockFilterChain>()));
  }

  MOCK_METHOD(Network::DrainableFilterChainSharedPtr, buildFilterChain,
              (const envoy::config::listener::v3::FilterChain&, FilterChainFactoryContextCreator&),
              (const));
};

class FcdsApiTest : public testing::Test {
public:
  FcdsApiTest() {
    ON_CALL(init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
  }

  void setup() {
    envoy::config::listener::v3::ListenerFcds fcds_config;
    //fcds_config.set_collection_name("test_listener_fcds_config_block");
    EXPECT_CALL(init_manager_, add(_));
   
    Stats::ScopeSharedPtr scope = stats_.createScope("testScope"); 
    fcds_ = std::make_unique<FcdsApi>(fcds_config, parent_context_, init_manager_,
                                      filter_chain_manager_, &filter_chain_factory_builder_, *scope);
    fcds_->initialize();
    fcds_callbacks_ = parent_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::MockManager init_manager_;
  Init::ManagerImpl fcds_init_manager_{"for_filter_chain_manager_test"};
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Stats::IsolatedStoreImpl store_;
  Stats::TestUtil::TestStore stats_;
  Config::SubscriptionCallbacks* fcds_callbacks_{};
  std::unique_ptr<FcdsApi> fcds_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  NiceMock<MockFilterChainFactoryBuilder> filter_chain_factory_builder_;
  NiceMock<Server::Configuration::MockFactoryContext> parent_context_;
  std::vector<Network::Address::InstanceConstSharedPtr> addresses_{
  	std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234)};
  FilterChainManagerImpl filter_chain_manager_{
      addresses_, parent_context_, fcds_init_manager_};
};

// Validate filter chain without name is rejected
TEST_F(FcdsApiTest, FilterChainWithoutNameIsRejected) {
  InSequence s;
  setup();

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("");
  filter_chain.add_filters();

  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  EXPECT_THROW_WITH_MESSAGE(
      fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, ""), EnvoyException,
      "Error adding/updating filter chain(s) : missing name in one or more filter chain\n\n");
}

// Validate filter chain with name is accepted
TEST_F(FcdsApiTest, FilterChainWithNameIsAccepted) {
  InSequence s;
  setup();

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("fc_01");
  filter_chain.mutable_filter_chain_match()->add_server_names("foo1.EXAMPLE.com");
  filter_chain.add_filters();

  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "");
}

// Validate empty filter chain update is accepted
TEST_F(FcdsApiTest, EmptyFcdsUpdateAccepted) {
  InSequence s;
  setup();
  fcds_callbacks_->onConfigUpdate({}, "");
}

// Validate empty filter chain updates with unque filter chain names are accepted
TEST_F(FcdsApiTest, FilterChainsWithUniqueMatchesAccepted) {
  InSequence s;
  setup();

  envoy::config::listener::v3::FilterChain filter_chain1;
  filter_chain1.set_name("fc_01");
  filter_chain1.mutable_filter_chain_match()->add_server_names("foo1.EXAMPLE.com");
  filter_chain1.add_filters();

  envoy::config::listener::v3::FilterChain filter_chain2;
  filter_chain2.set_name("fc_02");
  filter_chain1.mutable_filter_chain_match()->add_server_names("foo2.EXAMPLE.com");
  filter_chain2.add_filters();

  const auto decoded_resources = TestUtility::decodeResources({filter_chain1, filter_chain2});
  fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "");
}


// Validate onConfigUpdate throws EnvoyException with duplicate FilterChains.
// The first of the duplicates will be successfully applied, with the rest adding to
// the exception message.
TEST_F(FcdsApiTest, DuplicateFilterChainNamesAreRejected) {
  InSequence s;
  setup();

  envoy::config::listener::v3::FilterChain filter_chain1;
  filter_chain1.set_name("fc_01");
  filter_chain1.mutable_filter_chain_match()->add_server_names("foo1.EXAMPLE.com");
  filter_chain1.add_filters();

  envoy::config::listener::v3::FilterChain filter_chain2;
  filter_chain2.set_name("fc_01");
  filter_chain1.mutable_filter_chain_match()->add_server_names("foo1.EXAMPLE.com");
  filter_chain2.add_filters();

  const auto decoded_resources = TestUtility::decodeResources({filter_chain1, filter_chain1});
  EXPECT_THROW_WITH_MESSAGE(fcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, ""),
                            EnvoyException,
                            "Error adding/updating filter chain(s) : duplicate filter chain = fc_01\n\n");
}

// Validate behavior when the config fails delivery at the subscription level.
TEST_F(FcdsApiTest, FailureSubscription) {
  InSequence s;
  setup();

  fcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, {});
  EXPECT_EQ("", fcds_->versionInfo());
}


} // namespace
} // namespace Server
} // namespace Envoy

