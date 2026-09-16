#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/config/metadata.h"
#include "source/common/listener_manager/filter_chain_manager_impl.h"
#include "source/common/listener_manager/listener_impl.h"
#include "source/common/listener_manager/listener_info_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tls/ssl_socket.h"
#include "source/server/configuration_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/listener_component_factory.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

using Envoy::StatusHelpers::HasStatus;

namespace Envoy {
namespace Server {

class MockFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  MockFilterChainFactoryBuilder() {
    ON_CALL(*this, buildFilterChain(_, _, _))
        .WillByDefault(Return(std::make_shared<Network::MockFilterChain>()));
  }

  MOCK_METHOD(absl::StatusOr<Network::DrainableFilterChainSharedPtr>, buildFilterChain,
              (const envoy::config::listener::v3::FilterChain&, FilterChainFactoryContextCreator&,
               bool),
              (const));
};

class FilterChainManagerImplTest : public testing::TestWithParam<bool> {
public:
  struct DummyFcdsClientCallbacks : public FcdsClientCallbacks {
    void drainFilterChain(Network::DrainableFilterChainSharedPtr) override {}
  };
  DummyFcdsClientCallbacks dummy_fcds_callbacks_;
  envoy::config::core::v3::ConfigSource empty_config_source_;
  void SetUp() override {
    addresses_.emplace_back(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
    filter_chain_manager_ =
        std::make_unique<FilterChainManagerImpl>(addresses_, parent_context_, init_manager_);
    local_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    TestUtility::loadFromYaml(
        TestEnvironment::substitute(filter_chain_yaml, Network::Address::IpVersion::v4),
        filter_chain_template_);
    TestUtility::loadFromYaml(filter_chain_matcher, matcher_);
  }

  const Network::FilterChain*
  findFilterChainHelper(uint16_t destination_port, const std::string& destination_address,
                        const std::string& server_name, const std::string& transport_protocol,
                        const std::vector<std::string>& application_protocols,
                        const std::string& source_address, uint16_t source_port) {
    auto mock_socket = std::make_shared<NiceMock<Network::MockConnectionSocket>>();
    sockets_.push_back(mock_socket);

    if (absl::StartsWith(destination_address, "/")) {
      local_address_ = *Network::Address::PipeInstance::create(destination_address);
    } else {
      local_address_ =
          Network::Utility::parseInternetAddressNoThrow(destination_address, destination_port);
    }
    mock_socket->connection_info_provider_->setLocalAddress(local_address_);

    ON_CALL(*mock_socket, requestedServerName())
        .WillByDefault(Return(absl::AsciiStrToLower(server_name)));
    ON_CALL(*mock_socket, detectedTransportProtocol())
        .WillByDefault(Return(absl::string_view(transport_protocol)));
    ON_CALL(*mock_socket, requestedApplicationProtocols())
        .WillByDefault(ReturnRef(application_protocols));

    if (absl::StartsWith(source_address, "/")) {
      remote_address_ = *Network::Address::PipeInstance::create(source_address);
    } else {
      remote_address_ = Network::Utility::parseInternetAddressNoThrow(source_address, source_port);
    }
    mock_socket->connection_info_provider_->setRemoteAddress(remote_address_);
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    return filter_chain_manager_->findFilterChain(*mock_socket, stream_info);
  }

  void addSingleFilterChainHelper(
      const envoy::config::listener::v3::FilterChain& filter_chain,
      const envoy::config::listener::v3::FilterChain* fallback_filter_chain = nullptr) {
    THROW_IF_NOT_OK(filter_chain_manager_->addFilterChains(
        GetParam() ? &matcher_ : nullptr,
        std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain},
        fallback_filter_chain, filter_chain_factory_builder_, *filter_chain_manager_, nullptr,
        empty_config_source_, dummy_fcds_callbacks_));
  }

  // Intermediate states.
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  std::vector<std::shared_ptr<Network::MockConnectionSocket>> sockets_;

  // Reusable template.
  const std::string filter_chain_yaml = R"EOF(
      name: foo
      filter_chain_match:
        destination_port: 10000
      transport_socket:
        name: tls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
              - certificate_chain: { filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_cert.pem" }
                private_key: { filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_key.pem" }
          session_ticket_keys:
            keys:
            - filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
  )EOF";
  const std::string filter_chain_matcher = R"EOF(
     matcher_tree:
       input:
         name: port
         typed_config:
           "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput
       exact_match_map:
         map:
           "10000":
             action:
               name: foo
               typed_config:
                 "@type": type.googleapis.com/google.protobuf.StringValue
                 value: foo
  )EOF";
  Init::ManagerImpl init_manager_{"for_filter_chain_manager_test"};
  envoy::config::listener::v3::FilterChain filter_chain_template_;
  xds::type::matcher::v3::Matcher matcher_;
  std::shared_ptr<Network::MockFilterChain> build_out_filter_chain_{
      std::make_shared<Network::MockFilterChain>()};
  envoy::config::listener::v3::FilterChain fallback_filter_chain_;
  std::shared_ptr<Network::MockFilterChain> build_out_fallback_filter_chain_{
      std::make_shared<Network::MockFilterChain>()};

  NiceMock<MockFilterChainFactoryBuilder> filter_chain_factory_builder_;
  NiceMock<Server::Configuration::MockFactoryContext> parent_context_;
  std::vector<Network::Address::InstanceConstSharedPtr> addresses_;
  // Test target.
  std::unique_ptr<FilterChainManagerImpl> filter_chain_manager_;
};

TEST_P(FilterChainManagerImplTest, FilterChainMatchNothing) {
  auto filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(filter_chain, nullptr);
}

TEST_P(FilterChainManagerImplTest, FilterChainMatchCaseInSensitive) {
  envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
  new_filter_chain.mutable_filter_chain_match()->add_server_names("foo.EXAMPLE.com");
  EXPECT_OK(filter_chain_manager_->addFilterChains(
      GetParam() ? &matcher_ : nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&new_filter_chain}, nullptr,
      filter_chain_factory_builder_, *filter_chain_manager_, nullptr, empty_config_source_,
      dummy_fcds_callbacks_));
  auto filter_chain =
      findFilterChainHelper(10000, "127.0.0.1", "FOO.example.com", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
}

TEST_P(FilterChainManagerImplTest, AddSingleFilterChain) {
  addSingleFilterChainHelper(filter_chain_template_);
  {
    auto* filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
    EXPECT_NE(filter_chain, nullptr);
  }
  {
    auto* filter_chain = findFilterChainHelper(15000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
    EXPECT_EQ(filter_chain, nullptr);
  }
}

TEST_P(FilterChainManagerImplTest, FilterChainUseFallbackIfNoFilterChainMatches) {
  // The build helper will build matchable filter chain and then build the default filter chain.
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillOnce(Return(build_out_fallback_filter_chain_));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillOnce(Return(std::make_shared<Network::MockFilterChain>()))
      .RetiresOnSaturation();
  addSingleFilterChainHelper(filter_chain_template_, &fallback_filter_chain_);

  auto filter_chain = findFilterChainHelper(10000, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_NE(filter_chain, nullptr);
  auto fallback_filter_chain =
      findFilterChainHelper(9999, "127.0.0.1", "", "tls", {}, "8.8.8.8", 111);
  EXPECT_EQ(fallback_filter_chain, build_out_fallback_filter_chain_.get());
}

TEST_P(FilterChainManagerImplTest, LookupFilterChainContextByFilterChainMessage) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;

  for (int i = 0; i < 2; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check.
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _)).Times(2);
  EXPECT_OK(filter_chain_manager_->addFilterChains(
      GetParam() ? &matcher_ : nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_messages[0],
                                                                   &filter_chain_messages[1]},
      nullptr, filter_chain_factory_builder_, *filter_chain_manager_, nullptr, empty_config_source_,
      dummy_fcds_callbacks_));
}

TEST_P(FilterChainManagerImplTest, DuplicateContextsAreNotBuilt) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;

  for (int i = 0; i < 3; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }

  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _));
  EXPECT_OK(filter_chain_manager_->addFilterChains(
      GetParam() ? &matcher_ : nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_messages[0]},
      nullptr, filter_chain_factory_builder_, *filter_chain_manager_, nullptr, empty_config_source_,
      dummy_fcds_callbacks_));
  FilterChainManagerImpl new_filter_chain_manager{addresses_, parent_context_, init_manager_,
                                                  *filter_chain_manager_};
  // The new filter chain manager maintains 3 filter chains, but only 2 filter chain context is
  // built because it reuse the filter chain context in the previous filter chain manager
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _)).Times(2);
  EXPECT_OK(new_filter_chain_manager.addFilterChains(
      GetParam() ? &matcher_ : nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{
          &filter_chain_messages[0], &filter_chain_messages[1], &filter_chain_messages[2]},
      nullptr, filter_chain_factory_builder_, new_filter_chain_manager, nullptr,
      empty_config_source_, dummy_fcds_callbacks_));
}

TEST_P(FilterChainManagerImplTest, UpdateFilterChainsBetweenVersions) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;

  for (int i = 0; i < 2; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }

  auto filter_chain = std::make_shared<Network::MockFilterChain>();
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillOnce(Return(filter_chain));
  EXPECT_OK(filter_chain_manager_->addFilterChains(
      GetParam() ? &matcher_ : nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_messages[0]},
      nullptr, filter_chain_factory_builder_, *filter_chain_manager_, nullptr, empty_config_source_,
      dummy_fcds_callbacks_));

  FilterChainManagerImpl new_filter_chain_manager{addresses_, parent_context_, init_manager_,
                                                  *filter_chain_manager_};
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _));
  EXPECT_OK(new_filter_chain_manager.addFilterChains(
      GetParam() ? &matcher_ : nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_messages[1]},
      nullptr, filter_chain_factory_builder_, new_filter_chain_manager, nullptr,
      empty_config_source_, dummy_fcds_callbacks_));

  // The new filter chain manager is based on the previous filter chain manager, but it has a new
  // filter chain that is not in the previous filter chain manager, so we expect the previous
  // filter chains to be drained.
  EXPECT_EQ(filter_chain_manager_->drainingFilterChains().size(), 1);
  EXPECT_EQ(filter_chain_manager_->drainingFilterChains()[0], filter_chain);
}

TEST_P(FilterChainManagerImplTest, CreatedFilterChainFactoryContextHasIndependentDrainClose) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;
  for (int i = 0; i < 3; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("filter_chain_", i));
    // For sanity check
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }
  auto context0 = filter_chain_manager_->createFilterChainFactoryContext(&filter_chain_messages[0]);
  auto context1 = filter_chain_manager_->createFilterChainFactoryContext(&filter_chain_messages[1]);

  // Server as whole is not draining.
  MockDrainManager not_a_draining_manager;
  EXPECT_CALL(not_a_draining_manager, drainClose).WillRepeatedly(Return(false));
  Configuration::MockServerFactoryContext mock_server_context;
  EXPECT_CALL(mock_server_context, drainManager).WillRepeatedly(ReturnRef(not_a_draining_manager));
  EXPECT_CALL(parent_context_, serverFactoryContext).WillRepeatedly(ReturnRef(mock_server_context));

  EXPECT_FALSE(context0->drainDecision().drainClose(Network::DrainDirection::All));
  EXPECT_FALSE(context1->drainDecision().drainClose(Network::DrainDirection::All));

  // Drain filter chain 0
  auto* context_impl_0 = dynamic_cast<PerFilterChainFactoryContextImpl*>(context0.get());
  context_impl_0->startDraining();

  EXPECT_TRUE(context0->drainDecision().drainClose(Network::DrainDirection::All));
  EXPECT_FALSE(context1->drainDecision().drainClose(Network::DrainDirection::All));
}

TEST_P(FilterChainManagerImplTest, FilterChainFactoryContextDelegatesAccessors) {
  envoy::config::listener::v3::FilterChain filter_chain = filter_chain_template_;
  auto context = filter_chain_manager_->createFilterChainFactoryContext(&filter_chain);

  EXPECT_CALL(parent_context_, direction())
      .WillOnce(Return(envoy::config::core::v3::TrafficDirection::INBOUND));
  EXPECT_EQ(context->direction(), envoy::config::core::v3::TrafficDirection::INBOUND);

  EXPECT_CALL(parent_context_, isQuic()).WillOnce(Return(true));
  EXPECT_TRUE(context->isQuic());

  EXPECT_CALL(parent_context_, shouldBypassOverloadManager()).WillOnce(Return(true));
  EXPECT_TRUE(context->shouldBypassOverloadManager());

  context->scope();
  context->prefixedScope();
  EXPECT_EQ(&context->initManager(), &init_manager_);
  context->messageValidationVisitor();
  context->serverFactoryContext();

  EXPECT_ENVOY_BUG(std::ignore = context->drainDecision().addOnDrainCloseCb(
                       Network::DrainDirection::All, nullptr),
                   "Unexpected function call");
}

TEST_P(FilterChainManagerImplTest, DuplicateFilterChainMatchFails) {
  envoy::config::listener::v3::FilterChain new_filter_chain1 = filter_chain_template_;
  new_filter_chain1.mutable_filter_chain_match()->add_server_names("example.com");
  envoy::config::listener::v3::FilterChain new_filter_chain2 = new_filter_chain1;

  EXPECT_EQ(filter_chain_manager_
                ->addFilterChains(nullptr,
                                  std::vector<const envoy::config::listener::v3::FilterChain*>{
                                      &new_filter_chain1, &new_filter_chain2},
                                  nullptr, filter_chain_factory_builder_, *filter_chain_manager_,
                                  nullptr, empty_config_source_, dummy_fcds_callbacks_)
                .message(),
            "error adding listener '127.0.0.1:1234': filter chain 'foo' has the "
            "same matching rules defined as 'foo'"
#ifdef ENVOY_ENABLE_YAML
            ". duplicate matcher is: "
            "{\"destination_port\":10000,\"server_names\":[\"example.com\"]}"
#endif
  );
}

class MockFcdsClientCallbacks : public FcdsClientCallbacks {
public:
  MOCK_METHOD(void, drainFilterChain, (Network::DrainableFilterChainSharedPtr draining),
              (override));
};

TEST_P(FilterChainManagerImplTest, FcdsSharedFilterChainManagerBasic) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;

  auto fcds_shared_manager = std::make_shared<FcdsSharedFilterChainManager>(
      parent_context_.server_factory_context_, listener_component_factory);

  envoy::config::core::v3::ConfigSource config_source;
  config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::GRPC);
  config_source.mutable_api_config_source()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);

  std::string filter_chain_name = "dynamic_chain";

  Config::SubscriptionCallbacks* fcds_callbacks = nullptr;
  auto subscription = std::make_unique<NiceMock<Config::MockSubscription>>();
  auto* raw_subscription = subscription.get();

  EXPECT_CALL(*raw_subscription, start(testing::ElementsAre(filter_chain_name)));

  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Invoke([&fcds_callbacks, &subscription](
                           const envoy::config::core::v3::ConfigSource&, absl::string_view,
                           Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                           Config::OpaqueResourceDecoderSharedPtr,
                           const Config::SubscriptionOptions&) mutable
                           -> absl::StatusOr<Config::SubscriptionPtr> {
        fcds_callbacks = &callbacks;
        return std::move(subscription);
      }));

  MockFcdsClientCallbacks callbacks;
  auto handle_or_status =
      fcds_shared_manager->subscribe(config_source, filter_chain_name, callbacks, init_manager_);
  ASSERT_TRUE(handle_or_status.ok());
  auto handle = std::move(handle_or_status).value();

  Init::ExpectableWatcherImpl init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);

  ASSERT_NE(fcds_callbacks, nullptr);

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name(filter_chain_name);

  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillOnce(Return(Filter::NetworkFilterFactoriesList{}));

  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_OK(fcds_callbacks->onConfigUpdate(decoded_resources.refvec_, removed_resources, "v1"));

  const Network::FilterChain* active_chain =
      fcds_shared_manager->findThreadLocalFilterChain(filter_chain_name);
  ASSERT_NE(active_chain, nullptr);
  EXPECT_TRUE(active_chain->addedViaApi());

  envoy::config::listener::v3::FilterChain filter_chain_v2;
  filter_chain_v2.set_name(filter_chain_name);
  auto* filter = filter_chain_v2.add_filters();
  filter->set_name("dummy_filter");

  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillOnce(Return(Filter::NetworkFilterFactoriesList{}));

  Network::DrainableFilterChainSharedPtr drained_chain;
  EXPECT_CALL(callbacks, drainFilterChain(_))
      .WillOnce(Invoke([&drained_chain](Network::DrainableFilterChainSharedPtr draining) {
        drained_chain = draining;
      }));

  const auto decoded_resources_v2 = TestUtility::decodeResources({filter_chain_v2});
  EXPECT_OK(fcds_callbacks->onConfigUpdate(decoded_resources_v2.refvec_, removed_resources, "v2"));

  EXPECT_EQ(drained_chain.get(), active_chain);

  const Network::FilterChain* active_chain_v2 =
      fcds_shared_manager->findThreadLocalFilterChain(filter_chain_name);
  ASSERT_NE(active_chain_v2, nullptr);
  EXPECT_NE(active_chain_v2, active_chain);
  EXPECT_TRUE(active_chain_v2->addedViaApi());

  handle.reset();
}

// activeFilterChainNames() reports a chain only once it is warmed and committed, and drops it once
// the subscription is torn down. Mirrors the active set updateTlsState() publishes to workers.
TEST_P(FilterChainManagerImplTest, FcdsActiveFilterChainNames) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;

  auto fcds_shared_manager = std::make_shared<FcdsSharedFilterChainManager>(
      parent_context_.server_factory_context_, listener_component_factory);

  envoy::config::core::v3::ConfigSource config_source;
  config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::GRPC);
  config_source.mutable_api_config_source()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);

  std::string filter_chain_name = "dynamic_chain";

  Config::SubscriptionCallbacks* fcds_callbacks = nullptr;
  auto subscription = std::make_unique<NiceMock<Config::MockSubscription>>();
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Invoke([&fcds_callbacks, &subscription](
                           const envoy::config::core::v3::ConfigSource&, absl::string_view,
                           Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                           Config::OpaqueResourceDecoderSharedPtr,
                           const Config::SubscriptionOptions&) mutable
                           -> absl::StatusOr<Config::SubscriptionPtr> {
        fcds_callbacks = &callbacks;
        return std::move(subscription);
      }));

  MockFcdsClientCallbacks callbacks;
  auto handle_or_status =
      fcds_shared_manager->subscribe(config_source, filter_chain_name, callbacks, init_manager_);
  ASSERT_TRUE(handle_or_status.ok());
  auto handle = std::move(handle_or_status).value();

  // Subscribed but not yet warmed/committed: the chain is not active.
  EXPECT_THAT(fcds_shared_manager->activeFilterChainNames(), testing::IsEmpty());

  Init::ExpectableWatcherImpl init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);
  ASSERT_NE(fcds_callbacks, nullptr);

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name(filter_chain_name);
  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillOnce(Return(Filter::NetworkFilterFactoriesList{}));
  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(fcds_callbacks->onConfigUpdate(decoded_resources.refvec_, removed_resources, "v1"));

  // Committed: the chain is reported active.
  EXPECT_THAT(fcds_shared_manager->activeFilterChainNames(),
              testing::ElementsAre(absl::string_view(filter_chain_name)));

  // Destroyed (last handle released, subscription torn down): no longer reported.
  handle.reset();
  EXPECT_THAT(fcds_shared_manager->activeFilterChainNames(), testing::IsEmpty());
}

TEST_P(FilterChainManagerImplTest, FcdsNoMatcherFails) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = std::make_shared<FcdsSharedFilterChainManager>(
      parent_context_.server_factory_context_, listener_component_factory);

  auto status = filter_chain_manager_->addFilterChains(
      nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_template_},
      nullptr, filter_chain_factory_builder_, *filter_chain_manager_, fcds_shared_manager,
      empty_config_source_, dummy_fcds_callbacks_);

  EXPECT_THAT(status, HasStatus(absl::StatusCode::kInvalidArgument,
                                "FCDS requires a filter chain matcher."));
}

namespace {
// A GRPC config source accepted by the FCDS subscription machinery in tests.
envoy::config::core::v3::ConfigSource testGrpcConfigSource() {
  envoy::config::core::v3::ConfigSource config_source;
  config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::GRPC);
  config_source.mutable_api_config_source()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  return config_source;
}
} // namespace

// The accessors get_active_resource_names() relies on: a subscription is reported active only once
// its chain has committed, and the handle exposes the subscribed chain name. Before the commit the
// name is known but the chain is not active.
TEST_P(FilterChainManagerImplTest, FcdsHandleIsActiveTracksCommit) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = std::make_shared<FcdsSharedFilterChainManager>(
      parent_context_.server_factory_context_, listener_component_factory);
  const auto config_source = testGrpcConfigSource();
  const std::string name = "dynamic_chain";

  Config::SubscriptionCallbacks* fcds_callbacks = nullptr;
  auto subscription = std::make_unique<NiceMock<Config::MockSubscription>>();
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Invoke([&fcds_callbacks, &subscription](
                           const envoy::config::core::v3::ConfigSource&, absl::string_view,
                           Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                           Config::OpaqueResourceDecoderSharedPtr,
                           const Config::SubscriptionOptions&) mutable
                           -> absl::StatusOr<Config::SubscriptionPtr> {
        fcds_callbacks = &callbacks;
        return std::move(subscription);
      }));

  MockFcdsClientCallbacks callbacks;
  auto handle_or_status =
      fcds_shared_manager->subscribe(config_source, name, callbacks, init_manager_);
  ASSERT_TRUE(handle_or_status.ok());
  auto handle = std::move(handle_or_status).value();

  Init::ExpectableWatcherImpl init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);
  ASSERT_NE(fcds_callbacks, nullptr);

  // Subscribed but not yet committed: name is known, but the chain is not active.
  EXPECT_EQ(handle->filterChainName(), name);
  EXPECT_FALSE(handle->isActive());
  EXPECT_FALSE(fcds_shared_manager->isFilterChainActive(name));
  EXPECT_FALSE(fcds_shared_manager->isFilterChainActive("never_subscribed"));

  // Commit the chain.
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name(name);
  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillOnce(Return(Filter::NetworkFilterFactoriesList{}));
  const auto decoded = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed;
  EXPECT_OK(fcds_callbacks->onConfigUpdate(decoded.refvec_, removed, "v1"));

  // Now active.
  EXPECT_TRUE(handle->isActive());
  EXPECT_TRUE(fcds_shared_manager->isFilterChainActive(name));

  handle.reset();
}

// filterChainNames() reports an FCDS chain only when it is BOTH referenced by this listener's
// matcher (routable) AND committed (active). A chain committed in the process-wide shared manager
// but absent from this listener's matcher is not reported by this listener.
TEST_P(FilterChainManagerImplTest, FilterChainNamesFcdsRoutableAndActiveOnly) {
  // FCDS requires a matcher, so this is only meaningful in the matcher-enabled parameterization.
  if (!GetParam()) {
    GTEST_SKIP();
  }
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = std::make_shared<FcdsSharedFilterChainManager>(
      parent_context_.server_factory_context_, listener_component_factory);
  const auto config_source = testGrpcConfigSource();

  // Matcher routes port 10000 to the FCDS chain `fc_routable` (no inline chain of that name, so it
  // becomes an FCDS subscription rather than a static action).
  const std::string matcher_yaml = R"EOF(
     matcher_tree:
       input:
         name: port
         typed_config:
           "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput
       exact_match_map:
         map:
           "10000":
             action:
               name: filter-chain-name
               typed_config:
                 "@type": type.googleapis.com/google.protobuf.StringValue
                 value: fc_routable
  )EOF";
  xds::type::matcher::v3::Matcher fcds_matcher;
  TestUtility::loadFromYaml(matcher_yaml, fcds_matcher);

  // Capture the subscription callbacks per subscribe() call (one for `fc_routable` via
  // addFilterChains, one for the out-of-matcher chain below).
  std::vector<Config::SubscriptionCallbacks*> sub_callbacks;
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillRepeatedly(
          Invoke([&sub_callbacks](const envoy::config::core::v3::ConfigSource&, absl::string_view,
                                  Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                                  Config::OpaqueResourceDecoderSharedPtr,
                                  const Config::SubscriptionOptions&) mutable
                     -> absl::StatusOr<Config::SubscriptionPtr> {
            sub_callbacks.push_back(&callbacks);
            return std::make_unique<NiceMock<Config::MockSubscription>>();
          }));

  // No inline chains; the matcher's `fc_routable` is served via FCDS.
  EXPECT_OK(filter_chain_manager_->addFilterChains(
      &fcds_matcher, std::vector<const envoy::config::listener::v3::FilterChain*>{}, nullptr,
      filter_chain_factory_builder_, *filter_chain_manager_, fcds_shared_manager, config_source,
      dummy_fcds_callbacks_));

  Init::ExpectableWatcherImpl init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);
  ASSERT_FALSE(sub_callbacks.empty());

  // Subscribed (matcher references it) but not yet committed -> not reported.
  EXPECT_THAT(filter_chain_manager_->filterChainNames(), testing::IsEmpty());

  // Commit `fc_routable`. The factory list is move-only, so return a fresh one per call.
  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillRepeatedly(
          testing::InvokeWithoutArgs([] { return Filter::NetworkFilterFactoriesList{}; }));
  Protobuf::RepeatedPtrField<std::string> removed;
  {
    envoy::config::listener::v3::FilterChain fc;
    fc.set_name("fc_routable");
    const auto decoded = TestUtility::decodeResources({fc});
    EXPECT_OK(sub_callbacks.front()->onConfigUpdate(decoded.refvec_, removed, "v1"));
  }

  // Routable + active -> reported by this listener.
  EXPECT_THAT(filter_chain_manager_->filterChainNames(),
              testing::UnorderedElementsAre("fc_routable"));

  // A chain committed in the shared manager but NOT in this listener's matcher must not be reported
  // by this listener (routable-scoping): subscribe + commit `fc_elsewhere` via a separate handle.
  // It needs its own init manager (init_manager_ is already initialized above; adding a target to
  // an initialized manager is not allowed).
  Init::ManagerImpl other_init_manager{"fcds-other-test"};
  MockFcdsClientCallbacks other_callbacks;
  auto other_handle_or_status = fcds_shared_manager->subscribe(config_source, "fc_elsewhere",
                                                               other_callbacks, other_init_manager);
  ASSERT_TRUE(other_handle_or_status.ok());
  auto other_handle = std::move(other_handle_or_status).value();
  Init::ExpectableWatcherImpl other_watcher;
  EXPECT_CALL(other_watcher, ready());
  other_init_manager.initialize(other_watcher);
  ASSERT_GE(sub_callbacks.size(), 2u);
  {
    envoy::config::listener::v3::FilterChain fc;
    fc.set_name("fc_elsewhere");
    const auto decoded = TestUtility::decodeResources({fc});
    EXPECT_OK(sub_callbacks.back()->onConfigUpdate(decoded.refvec_, removed, "v1"));
  }
  EXPECT_TRUE(fcds_shared_manager->isFilterChainActive("fc_elsewhere"));

  // Still only `fc_routable`: the out-of-matcher (but active) chain is not reported by this
  // listener.
  EXPECT_THAT(filter_chain_manager_->filterChainNames(),
              testing::UnorderedElementsAre("fc_routable"));

  other_handle.reset();
}

TEST_P(FilterChainManagerImplTest, FcdsQuicFails) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = std::make_shared<FcdsSharedFilterChainManager>(
      parent_context_.server_factory_context_, listener_component_factory);

  EXPECT_CALL(parent_context_, isQuic()).WillRepeatedly(Return(true));

  auto status = filter_chain_manager_->addFilterChains(
      &matcher_,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_template_},
      nullptr, filter_chain_factory_builder_, *filter_chain_manager_, fcds_shared_manager,
      empty_config_source_, dummy_fcds_callbacks_);

  EXPECT_THAT(status, HasStatus(absl::StatusCode::kInvalidArgument,
                                "FCDS does not support QUIC filter chains"));
}

TEST_P(FilterChainManagerImplTest, FcdsInvalidConfigSourceFails) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = std::make_shared<FcdsSharedFilterChainManager>(
      parent_context_.server_factory_context_, listener_component_factory);

  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Return(absl::InvalidArgumentError("invalid config source specifier")));

  auto status = filter_chain_manager_->addFilterChains(
      &matcher_, {}, nullptr, filter_chain_factory_builder_, *filter_chain_manager_,
      fcds_shared_manager, empty_config_source_, dummy_fcds_callbacks_);

  EXPECT_THAT(status,
              HasStatus(absl::StatusCode::kInvalidArgument, "cannot create a filter chain matcher: "
                                                            "invalid config source specifier"));
}

INSTANTIATE_TEST_SUITE_P(Matcher, FilterChainManagerImplTest, ::testing::Values(true, false));

TEST(ListenerInfoImplTest, DefaultConstructor) {
  ListenerInfoImpl info;
  EXPECT_TRUE(info.name().empty());
  EXPECT_EQ(info.direction(), envoy::config::core::v3::TrafficDirection::UNSPECIFIED);
  EXPECT_FALSE(info.isQuic());
  EXPECT_FALSE(info.shouldBypassOverloadManager());
  info.metadata();
  info.typedMetadata();
}

TEST(ListenerInfoImplTest, DrainTypeConstructor) {
  ListenerInfoImpl info(envoy::config::listener::v3::Listener::MODIFY_ONLY);
  EXPECT_EQ(info.drainType(), envoy::config::listener::v3::Listener::MODIFY_ONLY);
  // Every other field keeps its default-constructed value.
  EXPECT_TRUE(info.name().empty());
  EXPECT_EQ(info.direction(), envoy::config::core::v3::TrafficDirection::UNSPECIFIED);
  EXPECT_FALSE(info.isQuic());
  EXPECT_FALSE(info.shouldBypassOverloadManager());
}

TEST(ListenerInfoImplTest, FromConfig) {
  envoy::config::listener::v3::Listener config;
  config.set_name("test_listener");
  config.set_traffic_direction(envoy::config::core::v3::TrafficDirection::INBOUND);
  ListenerInfoImpl info(config);
  EXPECT_EQ(info.name(), "test_listener");
  EXPECT_EQ(info.direction(), envoy::config::core::v3::TrafficDirection::INBOUND);
  EXPECT_FALSE(info.isQuic());
  info.metadata();
  info.typedMetadata();
}

TEST_P(FilterChainManagerImplTest, FilterChainNames) {
  std::vector<envoy::config::listener::v3::FilterChain> filter_chain_messages;
  for (int i = 0; i < 2; i++) {
    envoy::config::listener::v3::FilterChain new_filter_chain = filter_chain_template_;
    new_filter_chain.set_name(absl::StrCat("fc_", i));
    new_filter_chain.mutable_filter_chain_match()->mutable_destination_port()->set_value(10000 + i);
    filter_chain_messages.push_back(std::move(new_filter_chain));
  }
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillRepeatedly(testing::Invoke(
          [](const envoy::config::listener::v3::FilterChain& fc, FilterChainFactoryContextCreator&,
             bool) -> absl::StatusOr<Network::DrainableFilterChainSharedPtr> {
            auto chain = std::make_shared<NiceMock<Network::MockFilterChain>>();
            ON_CALL(*chain, name()).WillByDefault(Return(fc.name()));
            return chain;
          }));
  EXPECT_OK(filter_chain_manager_->addFilterChains(
      GetParam() ? &matcher_ : nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_messages[0],
                                                                   &filter_chain_messages[1]},
      nullptr, filter_chain_factory_builder_, *filter_chain_manager_, nullptr, empty_config_source_,
      dummy_fcds_callbacks_));
  EXPECT_THAT(filter_chain_manager_->filterChainNames(),
              testing::UnorderedElementsAre("fc_0", "fc_1"));
}

} // namespace Server
} // namespace Envoy
