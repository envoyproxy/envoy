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
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
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

class ListenerManagerImplTest : public testing::Test {
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
  ListenerManagerImplTest() : api_(Api::createApiForTest(server_.api_.random_)) {}

  void SetUp() override {
    ON_CALL(server_, api()).WillByDefault(ReturnRef(*api_));
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
              return ProdListenerComponentFactory::createNetworkFilterFactoryList_(
                  filters, filter_chain_factory_context);
            }));
    ON_CALL(listener_factory_, createListenerFilterFactoryList(_, _))
        .WillByDefault(
            Invoke([](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                          filters,
                      Configuration::ListenerFactoryContext& context)
                       -> std::vector<Network::ListenerFilterFactoryCb> {
              return ProdListenerComponentFactory::createListenerFilterFactoryList_(filters,
                                                                                    context);
            }));
    ON_CALL(listener_factory_, createUdpListenerFilterFactoryList(_, _))
        .WillByDefault(
            Invoke([](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                          filters,
                      Configuration::ListenerFactoryContext& context)
                       -> std::vector<Network::UdpListenerFilterFactoryCb> {
              return ProdListenerComponentFactory::createUdpListenerFilterFactoryList_(filters,
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
    socket_->address_provider_->setLocalAddress(local_address_);

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
    socket_->address_provider_->setRemoteAddress(remote_address_);

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
    socket_->address_provider_->setDirectRemoteAddressForTest(direct_remote_address_);

    return manager_->listeners().back().get().filterChainManager().findFilterChain(*socket_);
  }

  /**
   * Validate that createListenSocket is called once with the expected options.
   */
  void
  expectCreateListenSocket(const envoy::config::core::v3::SocketOption::SocketState& expected_state,
                           Network::Socket::Options::size_type expected_num_options,
                           ListenerComponentFactory::BindType bind_type = default_bind_type,
                           uint32_t worker_index = 0) {
    EXPECT_CALL(listener_factory_, createListenSocket(_, _, _, bind_type, worker_index))
        .WillOnce(
            Invoke([this, expected_num_options, &expected_state](
                       const Network::Address::InstanceConstSharedPtr&, Network::Socket::Type,
                       const Network::Socket::OptionsSharedPtr& options,
                       ListenerComponentFactory::BindType, uint32_t) -> Network::SocketSharedPtr {
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

  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  Api::OsSysCallsImpl os_sys_calls_actual_;
  NiceMock<MockInstance> server_;
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
};

} // namespace Server
} // namespace Envoy
