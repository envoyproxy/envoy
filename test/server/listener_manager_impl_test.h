#pragma once

#include <memory>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/server/configuration_impl.h"
#include "source/server/listener_manager_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/guard_dog.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_component_factory.h"
#include "test/mocks/server/worker.h"
#include "test/mocks/server/worker_factory.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

class ListenerHandle {
public:
  ListenerHandle(bool need_local_drain_manager = true) {
    if (need_local_drain_manager) {
      drain_manager_ = new MockDrainManager();
      EXPECT_CALL(*drain_manager_, startParentShutdownSequence()).Times(0);
    }
  }
  ~ListenerHandle() { onDestroy(); }

  MOCK_METHOD(void, onDestroy, ());

  Init::ExpectableTargetImpl target_;
  MockDrainManager* drain_manager_{};
  Configuration::FactoryContext* context_{};
};

class ListenerManagerImplTest : public testing::TestWithParam<bool> {
public:
  // reuse_port is the default on Linux for TCP. On other platforms even if set it is disabled
  // and the user is warned. For UDP it's always the default even if not effective.
  static constexpr ListenerComponentFactory::BindType default_bind_type =
#ifdef __linux__
      ListenerComponentFactory::BindType::ReusePort;
#else
      ListenerComponentFactory::BindType::NoReusePort;
#endif

protected:
  ListenerManagerImplTest()
      : api_(Api::createApiForTest(server_.api_.random_)), use_matcher_(GetParam()) {}

  void SetUp() override {
    ON_CALL(server_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(*server_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
    EXPECT_CALL(worker_factory_, createWorker_()).WillOnce(Return(worker_));
    ON_CALL(server_.validation_context_, staticValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor));
    ON_CALL(server_.validation_context_, dynamicValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor));
    manager_ =
        std::make_unique<ListenerManagerImpl>(server_, listener_factory_, worker_factory_,
                                              enable_dispatcher_stats_, server_.quic_stat_names_);

    // Use real filter loading by default.
    ON_CALL(listener_factory_, createNetworkFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
               Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context)
                -> std::vector<Network::FilterFactoryCb> {
              return ProdListenerComponentFactory::createNetworkFilterFactoryListImpl(
                  filters, filter_chain_factory_context);
            }));
    ON_CALL(listener_factory_, getTcpListenerConfigProviderManager())
        .WillByDefault(Return(&tcp_listener_config_provider_manager_));
    ON_CALL(listener_factory_, createListenerFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [this](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                       filters,
                   Configuration::ListenerFactoryContext& context)
                -> Filter::ListenerFilterFactoriesList {
              return ProdListenerComponentFactory::createListenerFilterFactoryListImpl(
                  filters, context, *listener_factory_.getTcpListenerConfigProviderManager());
            }));
    ON_CALL(listener_factory_, createUdpListenerFilterFactoryList(_, _))
        .WillByDefault(
            Invoke([](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                          filters,
                      Configuration::ListenerFactoryContext& context)
                       -> std::vector<Network::UdpListenerFilterFactoryCb> {
              return ProdListenerComponentFactory::createUdpListenerFilterFactoryListImpl(filters,
                                                                                          context);
            }));
    ON_CALL(listener_factory_, nextListenerTag()).WillByDefault(Invoke([this]() {
      return listener_tag_++;
    }));

    local_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234);
    EXPECT_CALL(os_sys_calls_, close(_)).WillRepeatedly(Return(Api::SysCallIntResult{0, errno}));
    EXPECT_CALL(os_sys_calls_, getsockname)
        .WillRepeatedly(Invoke([this](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) {
          return os_sys_calls_actual_.getsockname(sockfd, addr, addrlen);
        }));
    socket_ = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  }

  /**
   * This routing sets up an expectation that does various things:
   * 1) Allows us to track listener destruction via filter factory destruction.
   * 2) Allows us to register for init manager handling much like RDS, etc. would do.
   * 3) Stores the factory context for later use.
   * 4) Creates a mock local drain manager for the listener.
   */
  ListenerHandle* expectListenerCreate(bool need_init, bool added_via_api,
                                       envoy::config::listener::v3::Listener::DrainType drain_type =
                                           envoy::config::listener::v3::Listener::DEFAULT) {
    if (added_via_api) {
      EXPECT_CALL(server_.validation_context_, staticValidationVisitor()).Times(0);
      EXPECT_CALL(server_.validation_context_, dynamicValidationVisitor());
    } else {
      EXPECT_CALL(server_.validation_context_, staticValidationVisitor());
      EXPECT_CALL(server_.validation_context_, dynamicValidationVisitor()).Times(0);
    }
    auto raw_listener = new ListenerHandle();
    EXPECT_CALL(listener_factory_, createDrainManager_(drain_type))
        .WillOnce(Return(raw_listener->drain_manager_));
    EXPECT_CALL(listener_factory_, createNetworkFilterFactoryList(_, _))
        .WillOnce(Invoke(
            [raw_listener, need_init](
                const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>&,
                Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context)
                -> std::vector<Network::FilterFactoryCb> {
              std::shared_ptr<ListenerHandle> notifier(raw_listener);
              raw_listener->context_ = &filter_chain_factory_context;
              if (need_init) {
                filter_chain_factory_context.initManager().add(notifier->target_);
              }
              return {[notifier](Network::FilterManager&) -> void {}};
            }));

    return raw_listener;
  }

  ListenerHandle* expectListenerOverridden(bool need_init, ListenerHandle* origin = nullptr) {
    auto raw_listener = new ListenerHandle(false);
    // Simulate ListenerImpl: drain manager is copied from origin.
    if (origin != nullptr) {
      raw_listener->drain_manager_ = origin->drain_manager_;
    }
    // Overridden listener is always added by api.
    EXPECT_CALL(server_.validation_context_, staticValidationVisitor()).Times(0);
    EXPECT_CALL(server_.validation_context_, dynamicValidationVisitor());

    EXPECT_CALL(listener_factory_, createNetworkFilterFactoryList(_, _))
        .WillOnce(Invoke(
            [raw_listener, need_init](
                const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>&,
                Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context)
                -> std::vector<Network::FilterFactoryCb> {
              std::shared_ptr<ListenerHandle> notifier(raw_listener);
              raw_listener->context_ = &filter_chain_factory_context;
              if (need_init) {
                filter_chain_factory_context.initManager().add(notifier->target_);
              }
              return {[notifier](Network::FilterManager&) -> void {}};
            }));

    return raw_listener;
  }

  const Network::FilterChain*
  findFilterChain(uint16_t destination_port, const std::string& destination_address,
                  const std::string& server_name, const std::string& transport_protocol,
                  const std::vector<std::string>& application_protocols,
                  const std::string& source_address, uint16_t source_port,
                  std::string direct_source_address = "") {
    if (absl::StartsWith(destination_address, "/")) {
      local_address_ = std::make_shared<Network::Address::PipeInstance>(destination_address);
    } else {
      local_address_ =
          Network::Utility::parseInternetAddress(destination_address, destination_port);
    }
    socket_->connection_info_provider_->setLocalAddress(local_address_);

    socket_->connection_info_provider_->setRequestedServerName(server_name);
    ON_CALL(*socket_, requestedServerName()).WillByDefault(Return(absl::string_view(server_name)));
    ON_CALL(*socket_, detectedTransportProtocol())
        .WillByDefault(Return(absl::string_view(transport_protocol)));
    ON_CALL(*socket_, requestedApplicationProtocols())
        .WillByDefault(ReturnRef(application_protocols));

    if (absl::StartsWith(source_address, "/")) {
      remote_address_ = std::make_shared<Network::Address::PipeInstance>(source_address);
    } else {
      remote_address_ = Network::Utility::parseInternetAddress(source_address, source_port);
    }
    socket_->connection_info_provider_->setRemoteAddress(remote_address_);

    if (direct_source_address.empty()) {
      direct_source_address = source_address;
    }
    if (absl::StartsWith(direct_source_address, "/")) {
      direct_remote_address_ =
          std::make_shared<Network::Address::PipeInstance>(direct_source_address);
    } else {
      direct_remote_address_ =
          Network::Utility::parseInternetAddress(direct_source_address, source_port);
    }
    socket_->connection_info_provider_->setDirectRemoteAddressForTest(direct_remote_address_);
    NiceMock<StreamInfo::MockStreamInfo> stream_info;

    return manager_->listeners().back().get().filterChainManager().findFilterChain(*socket_,
                                                                                   stream_info);
  }

  /**
   * Validate that createListenSocket is called once with the expected options.
   */
  void
  expectCreateListenSocket(const envoy::config::core::v3::SocketOption::SocketState& expected_state,
                           Network::Socket::Options::size_type expected_num_options,
                           ListenerComponentFactory::BindType bind_type = default_bind_type,
                           uint32_t worker_index = 0) {
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, bind_type, _, worker_index))
        .WillOnce(
            Invoke([this, expected_num_options, &expected_state](
                       const Network::Address::InstanceConstSharedPtr&, Network::Socket::Type,
                       const Network::Socket::OptionsSharedPtr& options,
                       ListenerComponentFactory::BindType, const Network::SocketCreationOptions&,
                       uint32_t) -> Network::SocketSharedPtr {
              EXPECT_NE(options.get(), nullptr);
              EXPECT_EQ(options->size(), expected_num_options);
              EXPECT_TRUE(Network::Socket::applyOptions(options, *listener_factory_.socket_,
                                                        expected_state));
              return listener_factory_.socket_;
            }));
  }

  /**
   * Validate that setSocketOption() is called the expected number of times with the expected
   * options.
   */
  void expectSetsockopt(int expected_sockopt_level, int expected_sockopt_name, int expected_value,
                        uint32_t expected_num_calls = 1) {
    EXPECT_CALL(*listener_factory_.socket_,
                setSocketOption(expected_sockopt_level, expected_sockopt_name, _, sizeof(int)))
        .Times(expected_num_calls)
        .WillRepeatedly(Invoke(
            [expected_value](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
              EXPECT_EQ(expected_value, *static_cast<const int*>(optval));
              return {0, 0};
            }));
  }

  void checkStats(int line_num, uint64_t added, uint64_t modified, uint64_t removed,
                  uint64_t warming, uint64_t active, uint64_t draining,
                  uint64_t draining_filter_chains) {
    SCOPED_TRACE(line_num);

    EXPECT_EQ(added, server_.stats_store_.counter("listener_manager.listener_added").value());
    EXPECT_EQ(modified, server_.stats_store_.counter("listener_manager.listener_modified").value());
    EXPECT_EQ(removed, server_.stats_store_.counter("listener_manager.listener_removed").value());
    EXPECT_EQ(warming, server_.stats_store_
                           .gauge("listener_manager.total_listeners_warming",
                                  Stats::Gauge::ImportMode::NeverImport)
                           .value());
    EXPECT_EQ(active, server_.stats_store_
                          .gauge("listener_manager.total_listeners_active",
                                 Stats::Gauge::ImportMode::NeverImport)
                          .value());
    EXPECT_EQ(draining, server_.stats_store_
                            .gauge("listener_manager.total_listeners_draining",
                                   Stats::Gauge::ImportMode::NeverImport)
                            .value());
    EXPECT_EQ(draining_filter_chains, server_.stats_store_
                                          .gauge("listener_manager.total_filter_chains_draining",
                                                 Stats::Gauge::ImportMode::NeverImport)
                                          .value());
  }

  void checkConfigDump(
      const std::string& expected_dump_yaml,
      const Matchers::StringMatcher& name_matcher = Matchers::UniversalStringMatcher()) {
    auto message_ptr =
        server_.admin_.config_tracker_.config_tracker_callbacks_["listeners"](name_matcher);
    const auto& listeners_config_dump =
        dynamic_cast<const envoy::admin::v3::ListenersConfigDump&>(*message_ptr);
    envoy::admin::v3::ListenersConfigDump expected_listeners_config_dump;
    TestUtility::loadFromYaml(expected_dump_yaml, expected_listeners_config_dump);
    EXPECT_EQ(expected_listeners_config_dump.DebugString(), listeners_config_dump.DebugString());
  }

  bool addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                           const std::string& version_info = "", bool added_via_api = true) {
    // Automatically inject a matcher that always matches "foo" if not present.
    envoy::config::listener::v3::Listener listener;
    listener.MergeFrom(config);
    if (use_matcher_ && !listener.has_filter_chain_matcher()) {
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
        on_no_match:
          action:
            name: foo
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      )EOF";
      TestUtility::loadFromYaml(filter_chain_matcher, *listener.mutable_filter_chain_matcher());
    }
    return manager_->addOrUpdateListener(listener, version_info, added_via_api);
  }

  void testListenerUpdateWithSocketOptionsChange(const std::string& origin,
                                                 const std::string& updated) {
    InSequence s;

    EXPECT_CALL(*worker_, start(_, _));
    manager_->startWorkers(guard_dog_, callback_.AsStdFunction());

    auto socket = std::make_shared<testing::NiceMock<Network::MockListenSocket>>();

    ListenerHandle* listener_origin = expectListenerCreate(true, true);
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, default_bind_type, _, 0))
        .WillOnce(Return(socket));
    EXPECT_CALL(listener_origin->target_, initialize());
    EXPECT_TRUE(addOrUpdateListener(parseListenerFromV3Yaml(origin)));
    checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
    EXPECT_CALL(*worker_, addListener(_, _, _, _));
    listener_origin->target_.ready();
    worker_->callAddCompletion();
    EXPECT_EQ(1UL, manager_->listeners().size());
    checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

    ListenerHandle* listener_updated = expectListenerCreate(true, true);
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, default_bind_type, _, 0))
        .WillOnce(Return(socket));
    EXPECT_CALL(listener_updated->target_, initialize());
    EXPECT_TRUE(addOrUpdateListener(parseListenerFromV3Yaml(updated)));

    // Should be both active and warming now.
    EXPECT_EQ(1UL, manager_->listeners(ListenerManager::WARMING).size());
    EXPECT_EQ(1UL, manager_->listeners(ListenerManager::ACTIVE).size());
    checkStats(__LINE__, 1, 1, 0, 1, 1, 0, 0);

    EXPECT_CALL(*listener_updated, onDestroy());
    EXPECT_CALL(*listener_origin, onDestroy());
  }

  void testListenerUpdateWithSocketOptionsChangeRejected(const std::string& origin,
                                                         const std::string& updated,
                                                         const std::string& message) {
    InSequence s;

    EXPECT_CALL(*worker_, start(_, _));
    manager_->startWorkers(guard_dog_, callback_.AsStdFunction());

    auto socket = std::make_shared<testing::NiceMock<Network::MockListenSocket>>();

    ListenerHandle* listener_origin = expectListenerCreate(true, true);
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, default_bind_type, _, 0))
        .WillOnce(Return(socket));
    EXPECT_CALL(listener_origin->target_, initialize());
    EXPECT_TRUE(addOrUpdateListener(parseListenerFromV3Yaml(origin)));
    checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
    EXPECT_CALL(*worker_, addListener(_, _, _, _));
    listener_origin->target_.ready();
    worker_->callAddCompletion();
    EXPECT_EQ(1UL, manager_->listeners().size());
    checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

    ListenerHandle* listener_updated = expectListenerCreate(true, true);
    EXPECT_CALL(*listener_updated, onDestroy());
    EXPECT_THROW_WITH_MESSAGE(addOrUpdateListener(parseListenerFromV3Yaml(updated)), EnvoyException,
                              message);

    EXPECT_CALL(*listener_origin, onDestroy());
  }

  void testListenerUpdateWithSocketOptionsChangeDeprecatedBehavior(const std::string& origin,
                                                                   const std::string& updated) {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.enable_update_listener_socket_options", "false"}});
    InSequence s;

    EXPECT_CALL(*worker_, start(_, _));
    manager_->startWorkers(guard_dog_, callback_.AsStdFunction());

    ListenerHandle* listener_origin = expectListenerCreate(true, true);
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, default_bind_type, _, 0));
    EXPECT_CALL(listener_origin->target_, initialize());
    EXPECT_TRUE(addOrUpdateListener(parseListenerFromV3Yaml(origin)));
    checkStats(__LINE__, 1, 0, 0, 1, 0, 0, 0);
    EXPECT_CALL(*worker_, addListener(_, _, _, _));
    listener_origin->target_.ready();
    worker_->callAddCompletion();
    EXPECT_EQ(1UL, manager_->listeners().size());
    checkStats(__LINE__, 1, 0, 0, 0, 1, 0, 0);

    ListenerHandle* listener_updated = expectListenerCreate(true, true);
    EXPECT_CALL(*listener_factory_.socket_, duplicate());
    EXPECT_CALL(listener_updated->target_, initialize());
    EXPECT_TRUE(addOrUpdateListener(parseListenerFromV3Yaml(updated)));

    // Should be both active and warming now.
    EXPECT_EQ(1UL, manager_->listeners(ListenerManager::WARMING).size());
    EXPECT_EQ(1UL, manager_->listeners(ListenerManager::ACTIVE).size());
    checkStats(__LINE__, 1, 1, 0, 1, 1, 0, 0);

    EXPECT_CALL(*listener_updated, onDestroy());
    EXPECT_CALL(*listener_origin, onDestroy());
  }

  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  Api::OsSysCallsImpl os_sys_calls_actual_;
  NiceMock<MockInstance> server_;
  class DumbInternalListenerRegistry : public Singleton::Instance,
                                       public Network::InternalListenerRegistry {
  public:
    MOCK_METHOD(Network::LocalInternalListenerRegistry*, getLocalRegistry, ());
  };
  std::shared_ptr<DumbInternalListenerRegistry> internal_registry_{
      std::make_shared<DumbInternalListenerRegistry>()};

  NiceMock<MockListenerComponentFactory> listener_factory_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  MockWorker* worker_ = new MockWorker();
  NiceMock<MockWorkerFactory> worker_factory_;
  std::unique_ptr<ListenerManagerImpl> manager_;
  NiceMock<MockGuardDog> guard_dog_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  Network::Address::InstanceConstSharedPtr direct_remote_address_;
  std::unique_ptr<Network::MockConnectionSocket> socket_;
  uint64_t listener_tag_{1};
  bool enable_dispatcher_stats_{false};
  NiceMock<testing::MockFunction<void()>> callback_;
  // Test parameter indicating whether the unified filter chain matcher is enabled.
  bool use_matcher_;
  Filter::TcpListenerFilterConfigProviderManagerImpl tcp_listener_config_provider_manager_;
};
} // namespace Server
} // namespace Envoy
