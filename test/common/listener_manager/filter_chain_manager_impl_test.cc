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
#include "source/common/common/cleanup.h"
#include "source/common/config/metadata.h"
#include "source/common/config/resource_name.h"
#include "source/common/init/target_impl.h"
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
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/listener_component_factory.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

using Envoy::StatusHelpers::HasStatus;
using Envoy::StatusHelpers::IsOkAndHolds;

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

class MockFcdsListenerManagerCallbacks : public FcdsListenerManagerCallbacks {
public:
  MOCK_METHOD(void, notifyFilterChainUpdateCallbacks, (absl::string_view filter_chain_name));
};

class FilterChainManagerImplTest : public testing::TestWithParam<bool> {
public:
  struct DummyFcdsClientCallbacks : public FcdsClientCallbacks {
    void drainFilterChain(Network::DrainableFilterChainSharedPtr) override {}
  };
  DummyFcdsClientCallbacks dummy_fcds_callbacks_;
  envoy::config::core::v3::ConfigSource empty_config_source_;
  // Makes FCDS subscribe to the filter chains.
  envoy::config::core::v3::ConfigSource fcds_config_source_;
  void SetUp() override {
    fcds_config_source_.mutable_ads();
    ON_CALL(fcds_listener_manager_callbacks_, notifyFilterChainUpdateCallbacks(_))
        .WillByDefault(
            [this](absl::string_view name) { published_filter_chains_.emplace_back(name); });
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

  std::shared_ptr<FcdsSharedFilterChainManager>
  createFcdsSharedManager(ListenerComponentFactory& listener_component_factory) {
    // The manager takes ownership. The raw pointer lets the test drive the coalesced callback.
    fcds_tls_update_cb_ = new NiceMock<Event::MockSchedulableCallback>(
        &parent_context_.server_factory_context_.dispatcher_);
    return std::make_shared<FcdsSharedFilterChainManager>(parent_context_.server_factory_context_,
                                                          listener_component_factory,
                                                          fcds_listener_manager_callbacks_);
  }

  // Subscribes a local caller to the name of the filter chain and adds the filter chain.
  FcdsLocalSubscriptionHandleSharedPtr
  addLocalFilterChain(FcdsSharedFilterChainManager& fcds_shared_manager,
                      const envoy::config::listener::v3::FilterChain& filter_chain) {
    FcdsLocalSubscriptionHandleSharedPtr local =
        fcds_shared_manager.subscribeLocal(filter_chain.name()).value();
    EXPECT_THAT(local->update(filter_chain), IsOkAndHolds(true));
    return local;
  }

  void flushFcdsTlsUpdate() {
    if (fcds_tls_update_cb_ != nullptr && fcds_tls_update_cb_->enabled_) {
      fcds_tls_update_cb_->invokeCallback();
    }
  }

  // The pause trackers are fixture members so they outlive the manager built in each test.
  void setupFcdsUpdateMocks(MockListenerComponentFactory& listener_component_factory) {
    ON_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
            subscriptionFromConfigSource(_, _, _, _, _, _))
        .WillByDefault(
            [this](const envoy::config::core::v3::ConfigSource&, absl::string_view, Stats::Scope&,
                   Config::SubscriptionCallbacks& callbacks, Config::OpaqueResourceDecoderSharedPtr,
                   const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              fcds_subscription_callbacks_.push_back(&callbacks);
              return std::make_unique<NiceMock<Config::MockSubscription>>();
            });
    ON_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
        .WillByDefault([](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>&,
                          Configuration::FilterChainFactoryContext&)
                           -> absl::StatusOr<Filter::NetworkFilterFactoriesList> {
          return Filter::NetworkFilterFactoriesList{};
        });
    ON_CALL(parent_context_.server_factory_context_.xds_manager_,
            pause(fcds_filter_chain_type_url_))
        .WillByDefault([this](const std::string&) {
          fcds_xds_paused_ = true;
          fcds_pause_count_++;
          return std::make_unique<Cleanup>([this]() { fcds_xds_paused_ = false; });
        });
  }

  // Intermediate states.
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  std::vector<std::shared_ptr<Network::MockConnectionSocket>> sockets_;
  Event::MockSchedulableCallback* fcds_tls_update_cb_{};
  NiceMock<MockFcdsListenerManagerCallbacks> fcds_listener_manager_callbacks_;
  // The filter chains that the FCDS manager reported to the listener manager.
  std::vector<std::string> published_filter_chains_;
  std::vector<Config::SubscriptionCallbacks*> fcds_subscription_callbacks_;
  const std::string fcds_filter_chain_type_url_{
      Config::getTypeUrl<envoy::config::listener::v3::FilterChain>()};
  bool fcds_xds_paused_{false};
  int fcds_pause_count_{0};

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

TEST_P(FilterChainManagerImplTest, FilterChainNamesIncludeNamedDefaultChain) {
  auto chain = std::make_shared<NiceMock<Network::MockFilterChain>>();
  ON_CALL(*chain, name()).WillByDefault(Return("foo"));
  EXPECT_CALL(*build_out_fallback_filter_chain_, name()).WillRepeatedly(Return("default_chain"));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillOnce(Return(build_out_fallback_filter_chain_));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillOnce(Return(chain))
      .RetiresOnSaturation();
  addSingleFilterChainHelper(filter_chain_template_, &fallback_filter_chain_);

  EXPECT_THAT(filter_chain_manager_->filterChainNames(),
              testing::UnorderedElementsAre("foo", "default_chain"));
}

TEST_P(FilterChainManagerImplTest, FilterChainNamesOmitUnnamedDefaultChain) {
  auto chain = std::make_shared<NiceMock<Network::MockFilterChain>>();
  ON_CALL(*chain, name()).WillByDefault(Return("foo"));
  EXPECT_CALL(*build_out_fallback_filter_chain_, name()).WillRepeatedly(Return(""));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillOnce(Return(build_out_fallback_filter_chain_));
  EXPECT_CALL(filter_chain_factory_builder_, buildFilterChain(_, _, _))
      .WillOnce(Return(chain))
      .RetiresOnSaturation();
  addSingleFilterChainHelper(filter_chain_template_, &fallback_filter_chain_);

  EXPECT_THAT(filter_chain_manager_->filterChainNames(), testing::UnorderedElementsAre("foo"));
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

// A transport socket whose factory adds an init target to the init manager of its context.
class InitTargetTransportSocketConfigFactory
    : public Configuration::DownstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.init_target_for_test"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::BoolValue>();
  }
  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message&,
                               Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>&) override {
    context.initManager().add(init_target_);
    return std::make_unique<NiceMock<Network::MockDownstreamTransportSocketFactory>>();
  }

  Init::TargetImpl init_target_{"transport socket", [] {}};
};

TEST_P(FilterChainManagerImplTest, FcdsSharedFilterChainManagerBasic) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;

  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);

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
      .WillOnce([&fcds_callbacks, &subscription](const envoy::config::core::v3::ConfigSource&,
                                                 absl::string_view, Stats::Scope&,
                                                 Config::SubscriptionCallbacks& callbacks,
                                                 Config::OpaqueResourceDecoderSharedPtr,
                                                 const Config::SubscriptionOptions&) mutable
                    -> absl::StatusOr<Config::SubscriptionPtr> {
        fcds_callbacks = &callbacks;
        return std::move(subscription);
      });

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

  // The warmed chain reaches workers only after the coalesced callback runs.
  EXPECT_EQ(fcds_shared_manager->findThreadLocalFilterChain(filter_chain_name), nullptr);
  flushFcdsTlsUpdate();

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
      .WillOnce([&drained_chain](Network::DrainableFilterChainSharedPtr draining) {
        drained_chain = draining;
      });

  const auto decoded_resources_v2 = TestUtility::decodeResources({filter_chain_v2});
  EXPECT_OK(fcds_callbacks->onConfigUpdate(decoded_resources_v2.refvec_, removed_resources, "v2"));

  EXPECT_EQ(drained_chain.get(), active_chain);
  flushFcdsTlsUpdate();

  const Network::FilterChain* active_chain_v2 =
      fcds_shared_manager->findThreadLocalFilterChain(filter_chain_name);
  ASSERT_NE(active_chain_v2, nullptr);
  EXPECT_NE(active_chain_v2, active_chain);
  EXPECT_TRUE(active_chain_v2->addedViaApi());

  handle.reset();
}

// A subscription handle reports its chain active only once it is warmed and committed. Mirrors the
// active set updateTlsState() publishes to workers.
TEST_P(FilterChainManagerImplTest, FcdsSubscriptionHandleIsActive) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;

  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);

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
      .WillOnce([&fcds_callbacks, &subscription](const envoy::config::core::v3::ConfigSource&,
                                                 absl::string_view, Stats::Scope&,
                                                 Config::SubscriptionCallbacks& callbacks,
                                                 Config::OpaqueResourceDecoderSharedPtr,
                                                 const Config::SubscriptionOptions&) mutable
                    -> absl::StatusOr<Config::SubscriptionPtr> {
        fcds_callbacks = &callbacks;
        return std::move(subscription);
      });

  MockFcdsClientCallbacks callbacks;
  auto handle_or_status =
      fcds_shared_manager->subscribe(config_source, filter_chain_name, callbacks, init_manager_);
  ASSERT_TRUE(handle_or_status.ok());
  auto handle = std::move(handle_or_status).value();

  EXPECT_EQ(handle->filterChainName(), filter_chain_name);
  // Subscribed but not yet warmed/committed: the chain is not active.
  EXPECT_FALSE(handle->isActive());
  EXPECT_EQ(handle->activeFilterChain(), nullptr);

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
  EXPECT_TRUE(handle->isActive());
  flushFcdsTlsUpdate();
  ASSERT_NE(handle->activeFilterChain(), nullptr);
  EXPECT_EQ(handle->activeFilterChain().get(),
            fcds_shared_manager->findThreadLocalFilterChain(filter_chain_name));

  // Last handle released, subscription torn down: the name is no longer active.
  handle.reset();
  EXPECT_FALSE(fcds_shared_manager->isFilterChainActive(filter_chain_name));
  EXPECT_EQ(fcds_shared_manager->activeFilterChain(filter_chain_name), nullptr);
}

// A filter chain from FCDS warms until the init targets of its transport socket are ready. The
// server init manager does not get them, since it has completed once the server serves.
TEST_P(FilterChainManagerImplTest, FcdsFilterChainWarmsOnTransportSocketInitTargets) {
  InitTargetTransportSocketConfigFactory transport_socket_config_factory;
  Registry::InjectFactory<Configuration::DownstreamTransportSocketConfigFactory>
      registered_transport_socket_config_factory(transport_socket_config_factory);
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);

  envoy::config::core::v3::ConfigSource config_source;
  config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::GRPC);
  config_source.mutable_api_config_source()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  testing::StrictMock<MockFcdsClientCallbacks> callbacks;
  auto handle_or_status =
      fcds_shared_manager->subscribe(config_source, "chain", callbacks, init_manager_);
  ASSERT_TRUE(handle_or_status.ok());
  auto handle = std::move(handle_or_status).value();
  ASSERT_EQ(fcds_subscription_callbacks_.size(), 1);
  Init::ExpectableWatcherImpl init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);

  EXPECT_CALL(parent_context_.server_factory_context_.init_manager_, add(_)).Times(0);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  auto* transport_socket = filter_chain.mutable_transport_socket();
  transport_socket->set_name(transport_socket_config_factory.name());
  ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(Protobuf::BoolValue()));
  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(fcds_subscription_callbacks_[0]->onConfigUpdate(decoded_resources.refvec_,
                                                            removed_resources, "v1"));
  flushFcdsTlsUpdate();
  EXPECT_FALSE(handle->isActive());
  EXPECT_EQ(fcds_shared_manager->findThreadLocalFilterChain("chain"), nullptr);

  transport_socket_config_factory.init_target_.ready();
  EXPECT_TRUE(handle->isActive());
  flushFcdsTlsUpdate();
  EXPECT_NE(fcds_shared_manager->findThreadLocalFilterChain("chain"), nullptr);
}

// Warming a batch of filter chains in one event loop iteration publishes to workers only once.
TEST_P(FilterChainManagerImplTest, FcdsCoalescesThreadLocalUpdates) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);

  envoy::config::core::v3::ConfigSource config_source;
  config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::GRPC);
  config_source.mutable_api_config_source()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);

  testing::StrictMock<MockFcdsClientCallbacks> callbacks;
  const std::vector<std::string> names{"chain_a", "chain_b", "chain_c"};
  std::vector<FcdsSubscriptionHandleSharedPtr> handles;
  for (const auto& name : names) {
    auto handle_or_status =
        fcds_shared_manager->subscribe(config_source, name, callbacks, init_manager_);
    ASSERT_TRUE(handle_or_status.ok());
    handles.push_back(std::move(handle_or_status).value());
  }
  ASSERT_EQ(fcds_subscription_callbacks_.size(), names.size());

  Init::ExpectableWatcherImpl init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);

  // Warming the whole batch arms the coalesced callback exactly once.
  EXPECT_CALL(*fcds_tls_update_cb_, scheduleCallbackCurrentIteration());
  for (size_t i = 0; i < names.size(); i++) {
    envoy::config::listener::v3::FilterChain filter_chain;
    filter_chain.set_name(names[i]);
    const auto decoded_resources = TestUtility::decodeResources({filter_chain});
    Protobuf::RepeatedPtrField<std::string> removed_resources;
    EXPECT_OK(fcds_subscription_callbacks_[i]->onConfigUpdate(decoded_resources.refvec_,
                                                              removed_resources, "v1"));
  }

  // The publish is deferred, so workers observe nothing and discovery stays paused until the
  // callback runs. The batch pauses discovery exactly once.
  EXPECT_TRUE(fcds_xds_paused_);
  EXPECT_EQ(fcds_pause_count_, 1);
  for (const auto& name : names) {
    EXPECT_EQ(fcds_shared_manager->findThreadLocalFilterChain(name), nullptr);
  }

  // Running the single coalesced callback publishes the full snapshot and resumes discovery.
  fcds_tls_update_cb_->invokeCallback();
  EXPECT_FALSE(fcds_xds_paused_);
  for (const auto& name : names) {
    EXPECT_NE(fcds_shared_manager->findThreadLocalFilterChain(name), nullptr);
  }
  testing::Mock::VerifyAndClearExpectations(fcds_tls_update_cb_);

  // Removing a batch rearms the callback once and the removed chains stay visible until it runs.
  EXPECT_CALL(callbacks, drainFilterChain(_)).Times(2);
  EXPECT_CALL(*fcds_tls_update_cb_, scheduleCallbackCurrentIteration());
  for (size_t i = 0; i < 2; i++) {
    Protobuf::RepeatedPtrField<std::string> removed_resources;
    removed_resources.Add(std::string(names[i]));
    EXPECT_OK(fcds_subscription_callbacks_[i]->onConfigUpdate({}, removed_resources, "v2"));
  }
  EXPECT_NE(fcds_shared_manager->findThreadLocalFilterChain(names[0]), nullptr);
  EXPECT_TRUE(fcds_xds_paused_);
  EXPECT_EQ(fcds_pause_count_, 2);

  fcds_tls_update_cb_->invokeCallback();
  EXPECT_FALSE(fcds_xds_paused_);
  EXPECT_EQ(fcds_shared_manager->findThreadLocalFilterChain(names[0]), nullptr);
  EXPECT_EQ(fcds_shared_manager->findThreadLocalFilterChain(names[1]), nullptr);
  EXPECT_NE(fcds_shared_manager->findThreadLocalFilterChain(names[2]), nullptr);

  for (auto& handle : handles) {
    handle.reset();
  }
}

// Destroying the manager with a publish pending releases the xDS pause.
TEST_P(FilterChainManagerImplTest, FcdsReleasesPauseOnDestruction) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);

  envoy::config::core::v3::ConfigSource config_source;
  config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::GRPC);
  config_source.mutable_api_config_source()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);

  auto handle_or_status =
      fcds_shared_manager->subscribe(config_source, "chain", dummy_fcds_callbacks_, init_manager_);
  ASSERT_TRUE(handle_or_status.ok());
  auto handle = std::move(handle_or_status).value();
  ASSERT_EQ(fcds_subscription_callbacks_.size(), 1);

  Init::ExpectableWatcherImpl init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  const auto decoded_resources = TestUtility::decodeResources({filter_chain});
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(fcds_subscription_callbacks_[0]->onConfigUpdate(decoded_resources.refvec_,
                                                            removed_resources, "v1"));

  // The publish is pending, so discovery is paused.
  EXPECT_TRUE(fcds_xds_paused_);

  handle.reset();
  fcds_shared_manager.reset();
  EXPECT_FALSE(fcds_xds_paused_);
}

TEST_P(FilterChainManagerImplTest, FcdsNoMatcherFails) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);

  auto status = filter_chain_manager_->addFilterChains(
      nullptr,
      std::vector<const envoy::config::listener::v3::FilterChain*>{&filter_chain_template_},
      nullptr, filter_chain_factory_builder_, *filter_chain_manager_, fcds_shared_manager,
      empty_config_source_, dummy_fcds_callbacks_);

  EXPECT_THAT(status, HasStatus(absl::StatusCode::kInvalidArgument,
                                "FCDS requires a filter chain matcher."));
}

TEST_P(FilterChainManagerImplTest, FcdsQuicFails) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);

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
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);

  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Return(absl::InvalidArgumentError("invalid config source specifier")));

  auto status = filter_chain_manager_->addFilterChains(
      &matcher_, {}, nullptr, filter_chain_factory_builder_, *filter_chain_manager_,
      fcds_shared_manager, fcds_config_source_, dummy_fcds_callbacks_);

  EXPECT_THAT(status,
              HasStatus(absl::StatusCode::kInvalidArgument, "cannot create a filter chain matcher: "
                                                            "invalid config source specifier"));
}

// A local filter chain reaches the workers like one from FCDS, and the listeners that use its name
// find it without an FCDS subscription.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainIsFoundWithoutSubscription) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  EXPECT_CALL(fcds_listener_manager_callbacks_, notifyFilterChainUpdateCallbacks(_))
      .WillRepeatedly([this, &fcds_shared_manager](absl::string_view name) {
        // The workers have the filter chain before the listener manager learns its name.
        EXPECT_NE(fcds_shared_manager->findThreadLocalFilterChain(std::string(name)), nullptr);
        published_filter_chains_.emplace_back(name);
      });
  setupFcdsUpdateMocks(listener_component_factory);
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(0);

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  // A write of another filter chain keeps the publish coalesced.
  envoy::config::listener::v3::FilterChain other_filter_chain;
  other_filter_chain.set_name("other");
  const FcdsLocalSubscriptionHandleSharedPtr other_local =
      addLocalFilterChain(*fcds_shared_manager, other_filter_chain);
  EXPECT_EQ(fcds_shared_manager->findThreadLocalFilterChain("chain"), nullptr);
  EXPECT_TRUE(published_filter_chains_.empty());
  flushFcdsTlsUpdate();
  EXPECT_NE(fcds_shared_manager->findThreadLocalFilterChain("chain"), nullptr);
  EXPECT_THAT(published_filter_chains_, testing::UnorderedElementsAre("chain", "other"));

  testing::StrictMock<MockFcdsClientCallbacks> callbacks;
  auto handle_or_status =
      fcds_shared_manager->subscribe(empty_config_source_, "chain", callbacks, init_manager_);
  ASSERT_TRUE(handle_or_status.ok());
  auto handle = std::move(handle_or_status).value();
  EXPECT_TRUE(handle->isActive());
  testing::StrictMock<Init::ExpectableWatcherImpl> init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);
  EXPECT_EQ(handle->filterChain(), fcds_shared_manager->findThreadLocalFilterChain("chain"));
}

// An update of a local filter chain drains the replaced version, and an unchanged one does nothing.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainUpdateDrainsReplacedVersion) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  testing::StrictMock<MockFcdsClientCallbacks> callbacks;
  auto handle =
      fcds_shared_manager->subscribe(empty_config_source_, "chain", callbacks, init_manager_);
  ASSERT_TRUE(handle.ok());
  const Network::DrainableFilterChainSharedPtr first_version = (*handle)->activeFilterChain();

  filter_chain.mutable_transport_socket_connect_timeout()->set_seconds(1);
  EXPECT_CALL(callbacks, drainFilterChain(first_version));
  EXPECT_THAT(local->update(filter_chain), IsOkAndHolds(true));
  EXPECT_NE((*handle)->activeFilterChain(), first_version);

  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _)).Times(0);
  EXPECT_THAT(local->update(filter_chain), IsOkAndHolds(false));
}

// The subscription of a local caller keeps its filter chain once no listener uses it, and a removal
// drains the listeners that do. A listener that uses the name after the removal waits for the next
// write.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainRemovalDrainsTheListenersThatUseIt) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  flushFcdsTlsUpdate();

  {
    auto unused_handle = fcds_shared_manager->subscribe(empty_config_source_, "chain",
                                                        dummy_fcds_callbacks_, init_manager_);
    ASSERT_TRUE(unused_handle.ok());
  }
  EXPECT_TRUE(fcds_shared_manager->isFilterChainActive("chain"));

  testing::StrictMock<MockFcdsClientCallbacks> callbacks;
  auto handle =
      fcds_shared_manager->subscribe(empty_config_source_, "chain", callbacks, init_manager_);
  ASSERT_TRUE(handle.ok());
  EXPECT_CALL(callbacks, drainFilterChain((*handle)->activeFilterChain()));
  local->remove();
  local.reset();
  EXPECT_FALSE(fcds_shared_manager->isFilterChainActive("chain"));
  flushFcdsTlsUpdate();
  EXPECT_EQ(fcds_shared_manager->findThreadLocalFilterChain("chain"), nullptr);

  Init::ManagerImpl later_init_manager("later");
  auto later_handle = fcds_shared_manager->subscribe(empty_config_source_, "chain",
                                                     dummy_fcds_callbacks_, later_init_manager);
  ASSERT_TRUE(later_handle.ok());
  testing::StrictMock<Init::ExpectableWatcherImpl> later_init_watcher;
  later_init_manager.initialize(later_init_watcher);

  // The listener still uses the name, so a later write serves it again.
  EXPECT_CALL(later_init_watcher, ready());
  local = addLocalFilterChain(*fcds_shared_manager, filter_chain);
  EXPECT_TRUE((*handle)->isActive());
  later_handle->reset();
  EXPECT_CALL(callbacks, drainFilterChain(_));
  local->remove();
  local.reset();

  // Once neither a listener nor a local caller uses the name, FCDS can subscribe to it.
  handle->reset();
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _));
  EXPECT_TRUE(fcds_shared_manager
                  ->subscribe(fcds_config_source_, "chain", dummy_fcds_callbacks_, init_manager_)
                  .ok());
}

// A local filter chain that is removed while it warms is not committed, and a later write of the
// same configuration builds it again.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainRemovedWhileWarming) {
  InitTargetTransportSocketConfigFactory transport_socket_config_factory;
  Registry::InjectFactory<Configuration::DownstreamTransportSocketConfigFactory>
      registered_transport_socket_config_factory(transport_socket_config_factory);
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  auto* transport_socket = filter_chain.mutable_transport_socket();
  transport_socket->set_name(transport_socket_config_factory.name());
  ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(Protobuf::BoolValue()));
  // The listener keeps the state of the name after the removal.
  auto handle = fcds_shared_manager->subscribe(empty_config_source_, "chain", dummy_fcds_callbacks_,
                                               init_manager_);
  ASSERT_TRUE(handle.ok());
  testing::StrictMock<Init::ExpectableWatcherImpl> init_watcher;
  init_manager_.initialize(init_watcher);

  FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  // The removal ends the wait of the listener.
  EXPECT_CALL(init_watcher, ready());
  local->remove();
  local.reset();
  testing::Mock::VerifyAndClearExpectations(&init_watcher);
  transport_socket_config_factory.init_target_.ready();
  EXPECT_FALSE(fcds_shared_manager->isFilterChainActive("chain"));

  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _));
  local = addLocalFilterChain(*fcds_shared_manager, filter_chain);
}

// A name that FCDS subscribes to cannot be provided locally, and a listener without an FCDS config
// source cannot wait for it.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainRejectsDiscoveredName) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  auto handle = fcds_shared_manager->subscribe(fcds_config_source_, "chain", dummy_fcds_callbacks_,
                                               init_manager_);
  ASSERT_TRUE(handle.ok());

  EXPECT_THAT(
      fcds_shared_manager->subscribeLocal("chain"),
      HasStatus(absl::StatusCode::kAlreadyExists, "filter chain chain is discovered through FCDS"));
  EXPECT_THAT(
      fcds_shared_manager->subscribe(empty_config_source_, "chain", dummy_fcds_callbacks_,
                                     init_manager_),
      HasStatus(absl::StatusCode::kAlreadyExists, "filter chain chain is discovered through FCDS"));
}

// A listener with an FCDS config source cannot use the name of a local filter chain, also while
// another listener waits for it to be added.
TEST_P(FilterChainManagerImplTest, FcdsSubscriptionRejectsLocalName) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(0);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("added");
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  EXPECT_THAT(fcds_shared_manager->subscribe(fcds_config_source_, "added", dummy_fcds_callbacks_,
                                             init_manager_),
              HasStatus(absl::StatusCode::kAlreadyExists,
                        "filter chain added is added or awaited locally"));

  auto handle = fcds_shared_manager->subscribe(empty_config_source_, "awaited",
                                               dummy_fcds_callbacks_, init_manager_);
  ASSERT_TRUE(handle.ok());
  EXPECT_THAT(fcds_shared_manager->subscribe(fcds_config_source_, "awaited", dummy_fcds_callbacks_,
                                             init_manager_),
              HasStatus(absl::StatusCode::kAlreadyExists,
                        "filter chain awaited is added or awaited locally"));
}

// A local filter chain that cannot be built returns the error, also when its build throws, and
// leaves no state behind once the local caller drops its subscription.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainThatFailsToBuildLeavesNoState) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillOnce(Return(absl::InvalidArgumentError("bad filter")))
      .WillOnce(testing::Throw(EnvoyException("bad filter config")));
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  FcdsLocalSubscriptionHandleSharedPtr local = fcds_shared_manager->subscribeLocal("chain").value();
  EXPECT_THAT(local->update(filter_chain),
              HasStatus(absl::StatusCode::kInvalidArgument, "bad filter"));
  EXPECT_THAT(local->update(filter_chain),
              HasStatus(absl::StatusCode::kInvalidArgument, "bad filter config"));
  local.reset();

  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _));
  EXPECT_TRUE(fcds_shared_manager
                  ->subscribe(fcds_config_source_, "chain", dummy_fcds_callbacks_, init_manager_)
                  .ok());
}

// A failed update of a local filter chain keeps the version that serves.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainFailedUpdateKeepsTheActiveVersion) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  const Network::DrainableFilterChainSharedPtr active =
      fcds_shared_manager->activeFilterChain("chain");
  ASSERT_NE(active, nullptr);

  filter_chain.mutable_transport_socket_connect_timeout()->set_seconds(1);
  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillOnce(Return(absl::InvalidArgumentError("bad filter")));
  EXPECT_THAT(local->update(filter_chain),
              HasStatus(absl::StatusCode::kInvalidArgument, "bad filter"));
  EXPECT_EQ(active, fcds_shared_manager->activeFilterChain("chain"));
  local->remove();
  local.reset();

  // No listener used the name, so FCDS can subscribe to it at once.
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _));
  EXPECT_TRUE(fcds_shared_manager
                  ->subscribe(fcds_config_source_, "chain", dummy_fcds_callbacks_, init_manager_)
                  .ok());
}

// The subscription of the local caller keeps the state of the name when the last listener goes away
// inside its write.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainWriteOutlivesTheLastListener) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  auto handle = fcds_shared_manager->subscribe(empty_config_source_, "chain", dummy_fcds_callbacks_,
                                               init_manager_);
  ASSERT_TRUE(handle.ok());
  testing::StrictMock<Init::ExpectableWatcherImpl> init_watcher;
  init_manager_.initialize(init_watcher);
  // The listener warms inside the write, fails to listen and goes away.
  EXPECT_CALL(init_watcher, ready()).WillOnce([&handle]() { handle->reset(); });
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  EXPECT_TRUE(fcds_shared_manager->isFilterChainActive("chain"));
}

// The subscription of the local caller keeps the state of the name when the last listener goes away
// inside its removal.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainRemovalOutlivesTheLastListener) {
  InitTargetTransportSocketConfigFactory transport_socket_config_factory;
  Registry::InjectFactory<Configuration::DownstreamTransportSocketConfigFactory>
      registered_transport_socket_config_factory(transport_socket_config_factory);
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  auto* transport_socket = filter_chain.mutable_transport_socket();
  transport_socket->set_name(transport_socket_config_factory.name());
  ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(Protobuf::BoolValue()));
  auto handle = fcds_shared_manager->subscribe(empty_config_source_, "chain", dummy_fcds_callbacks_,
                                               init_manager_);
  ASSERT_TRUE(handle.ok());
  testing::StrictMock<Init::ExpectableWatcherImpl> init_watcher;
  init_manager_.initialize(init_watcher);
  FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  // The removal ends the wait, and the listener fails to listen and goes away.
  EXPECT_CALL(init_watcher, ready()).WillOnce([&handle]() { handle->reset(); });
  local->remove();
  Init::ManagerImpl fcds_init_manager("fcds");
  EXPECT_THAT(fcds_shared_manager->subscribe(fcds_config_source_, "chain", dummy_fcds_callbacks_,
                                             fcds_init_manager),
              HasStatus(absl::StatusCode::kAlreadyExists,
                        "filter chain chain is added or awaited locally"));

  local.reset();
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _));
  EXPECT_TRUE(
      fcds_shared_manager
          ->subscribe(fcds_config_source_, "chain", dummy_fcds_callbacks_, fcds_init_manager)
          .ok());
}

// A listener without an FCDS config source waits for a local caller to add the filter chain, also
// after a write that cannot be built.
TEST_P(FilterChainManagerImplTest, FcdsSubscriptionWithoutConfigSourceWaitsForLocalFilterChain) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(0);
  auto handle = fcds_shared_manager->subscribe(empty_config_source_, "chain", dummy_fcds_callbacks_,
                                               init_manager_);
  ASSERT_TRUE(handle.ok());
  testing::StrictMock<Init::ExpectableWatcherImpl> init_watcher;
  init_manager_.initialize(init_watcher);
  EXPECT_FALSE((*handle)->isActive());

  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  EXPECT_CALL(listener_component_factory, createNetworkFilterFactoryList(_, _))
      .WillOnce(Return(absl::InvalidArgumentError("bad filter")))
      .WillOnce(testing::DoDefault());
  FcdsLocalSubscriptionHandleSharedPtr local = fcds_shared_manager->subscribeLocal("chain").value();
  EXPECT_THAT(local->update(filter_chain),
              HasStatus(absl::StatusCode::kInvalidArgument, "bad filter"));
  // The local caller keeps nothing of a first write that fails.
  local.reset();
  EXPECT_FALSE((*handle)->isActive());

  EXPECT_CALL(init_watcher, ready());
  local = addLocalFilterChain(*fcds_shared_manager, filter_chain);
  EXPECT_TRUE((*handle)->isActive());
}

// A name that a listener without an FCDS config source waits for is released once the listener goes
// away.
TEST_P(FilterChainManagerImplTest, FcdsSubscriptionWithoutConfigSourceReleasesTheName) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  EXPECT_TRUE(fcds_shared_manager
                  ->subscribe(empty_config_source_, "chain", dummy_fcds_callbacks_, init_manager_)
                  .ok());

  EXPECT_CALL(parent_context_.server_factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _));
  EXPECT_TRUE(fcds_shared_manager
                  ->subscribe(fcds_config_source_, "chain", dummy_fcds_callbacks_, init_manager_)
                  .ok());
}

// A publish reports the filter chains from FCDS too, but not a filter chain removed before it.
TEST_P(FilterChainManagerImplTest, FcdsPublishReportsCommittedFilterChains) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  auto handle = fcds_shared_manager->subscribe(fcds_config_source_, "discovered",
                                               dummy_fcds_callbacks_, init_manager_);
  ASSERT_TRUE(handle.ok());
  testing::StrictMock<Init::ExpectableWatcherImpl> init_watcher;
  EXPECT_CALL(init_watcher, ready());
  init_manager_.initialize(init_watcher);
  envoy::config::listener::v3::FilterChain discovered;
  discovered.set_name("discovered");
  const auto decoded_resources = TestUtility::decodeResources({discovered});
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(fcds_subscription_callbacks_[0]->onConfigUpdate(decoded_resources.refvec_,
                                                            removed_resources, "v1"));

  envoy::config::listener::v3::FilterChain removed;
  removed.set_name("removed");
  // The local caller keeps the state of the name through the publish.
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, removed);
  local->remove();
  flushFcdsTlsUpdate();
  EXPECT_THAT(published_filter_chains_, testing::ElementsAre("discovered"));
}

// A write of a local filter chain whose committed version waits for its publish publishes that
// version first, so the listener manager learns about it before the next version warms.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainUpdatePublishesTheCommittedVersionFirst) {
  InitTargetTransportSocketConfigFactory transport_socket_config_factory;
  Registry::InjectFactory<Configuration::DownstreamTransportSocketConfigFactory>
      registered_transport_socket_config_factory(transport_socket_config_factory);
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  EXPECT_TRUE(published_filter_chains_.empty());
  // The next version warms until its transport socket is ready.
  auto* transport_socket = filter_chain.mutable_transport_socket();
  transport_socket->set_name(transport_socket_config_factory.name());
  ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(Protobuf::BoolValue()));
  ASSERT_THAT(local->update(filter_chain), IsOkAndHolds(true));
  EXPECT_THAT(published_filter_chains_, testing::ElementsAre("chain"));
  EXPECT_FALSE(fcds_tls_update_cb_->enabled_);

  transport_socket_config_factory.init_target_.ready();
  flushFcdsTlsUpdate();
  EXPECT_THAT(published_filter_chains_, testing::ElementsAre("chain", "chain"));
}

// A write of a local filter chain that commits at once publishes the committed version before it
// builds the next one.
TEST_P(FilterChainManagerImplTest, FcdsLocalFilterChainCommittedAtOncePublishesTheEarlierVersion) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  const Network::DrainableFilterChainSharedPtr first_version =
      fcds_shared_manager->activeFilterChain("chain");

  filter_chain.mutable_transport_socket_connect_timeout()->set_seconds(1);
  ASSERT_THAT(local->update(filter_chain), IsOkAndHolds(true));
  EXPECT_THAT(published_filter_chains_, testing::ElementsAre("chain"));
  EXPECT_EQ(first_version.get(), fcds_shared_manager->findThreadLocalFilterChain("chain"));
  EXPECT_TRUE(fcds_tls_update_cb_->enabled_);

  flushFcdsTlsUpdate();
  EXPECT_THAT(published_filter_chains_, testing::ElementsAre("chain", "chain"));
  EXPECT_EQ(fcds_shared_manager->activeFilterChain("chain").get(),
            fcds_shared_manager->findThreadLocalFilterChain("chain"));
}

// A worker that drops the last reference to a replaced filter chain hands it to the main thread,
// since its filters own objects of the main thread.
TEST_P(FilterChainManagerImplTest, FcdsReplacedFilterChainIsDeletedOnTheMainThread) {
  NiceMock<MockListenerComponentFactory> listener_component_factory;
  auto fcds_shared_manager = createFcdsSharedManager(listener_component_factory);
  setupFcdsUpdateMocks(listener_component_factory);
  envoy::config::listener::v3::FilterChain filter_chain;
  filter_chain.set_name("chain");
  const FcdsLocalSubscriptionHandleSharedPtr local =
      addLocalFilterChain(*fcds_shared_manager, filter_chain);
  flushFcdsTlsUpdate();
  const std::weak_ptr<Network::DrainableFilterChain> first_version =
      fcds_shared_manager->activeFilterChain("chain");
  // The states that a worker still holds.
  std::vector<ThreadLocal::ThreadLocalObjectSharedPtr> worker_states =
      parent_context_.server_factory_context_.thread_local_.data_;

  filter_chain.mutable_transport_socket_connect_timeout()->set_seconds(1);
  ASSERT_THAT(local->update(filter_chain), IsOkAndHolds(true));
  flushFcdsTlsUpdate();
  // The main dispatcher reports thread safety on every thread until it runs.
  ON_CALL(parent_context_.server_factory_context_.dispatcher_, isThreadSafe())
      .WillByDefault(Return(true));
  Event::DispatcherThreadDeletableConstPtr deletable;
  EXPECT_CALL(parent_context_.server_factory_context_.dispatcher_, deleteInDispatcherThread(_))
      .WillOnce([&deletable](Event::DispatcherThreadDeletableConstPtr state) {
        deletable = std::move(state);
      });
  Thread::threadFactoryForTest()
      .createThread([&worker_states]() { worker_states.clear(); })
      ->join();
  EXPECT_FALSE(first_version.expired());
  deletable.reset();
  EXPECT_TRUE(first_version.expired());
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

} // namespace Server
} // namespace Envoy
