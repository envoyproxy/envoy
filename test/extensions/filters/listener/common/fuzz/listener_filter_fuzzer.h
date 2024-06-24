#pragma once

#include "envoy/network/filter.h"

#include "source/common/listener_manager/connection_handler_impl.h"
#include "source/common/network/connection_balancer_impl.h"

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fakes.h"
#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.pb.validate.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class ListenerFilterFuzzer {
public:
  ListenerFilterFuzzer() {
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket_));
    ON_CALL(cb_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher_));
    ON_CALL(cb_, dynamicMetadata()).WillByDefault(testing::ReturnRef(metadata_));
    ON_CALL(Const(cb_), dynamicMetadata()).WillByDefault(testing::ReturnRef(metadata_));
  }

  void fuzz(Network::ListenerFilterPtr filter,
            const test::extensions::filters::listener::FilterFuzzTestCase& input);

private:
  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  FakeConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  envoy::config::core::v3::Metadata metadata_;
};

class ListenerFilterWithDataFuzzer : public Network::ListenerConfig,
                                     public Network::FilterChainManager {
public:
  ListenerFilterWithDataFuzzer();

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return *this; }
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  std::vector<Network::ListenSocketFactoryPtr>& listenSocketFactories() override {
    return socket_factories_;
  }
  bool bindToPort() const override { return true; }
  bool handOffRestoredDestinationConnections() const override { return false; }
  uint32_t perConnectionBufferLimitBytes() const override { return 0; }
  std::chrono::milliseconds listenerFiltersTimeout() const override { return {}; }
  bool continueOnListenerFiltersTimeout() const override { return false; }
  Stats::Scope& listenerScope() override { return *stats_store_.rootScope(); }
  uint64_t listenerTag() const override { return 1; }
  ResourceLimit& openConnections() override { return open_connections_; }
  const std::string& name() const override { return name_; }
  Network::UdpListenerConfigOptRef udpListenerConfig() override { return {}; }
  Network::InternalListenerConfigOptRef internalListenerConfig() override { return {}; }
  const Network::ListenerInfoConstSharedPtr& listenerInfo() const override {
    return listener_info_;
  }
  Network::ConnectionBalancer& connectionBalancer(const Network::Address::Instance&) override {
    return connection_balancer_;
  }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return empty_access_logs_;
  }
  uint32_t tcpBacklogSize() const override { return ENVOY_TCP_BACKLOG_SIZE; }
  uint32_t maxConnectionsToAcceptPerSocketEvent() const override {
    return Network::DefaultMaxConnectionsToAcceptPerSocketEvent;
  }
  Init::Manager& initManager() override { return *init_manager_; }
  bool ignoreGlobalConnLimit() const override { return false; }
  bool shouldBypassOverloadManager() const override { return false; }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&,
                                              const StreamInfo::StreamInfo&) const override {
    return filter_chain_.get();
  }

  void fuzz(Network::ListenerFilterPtr filter,
            const test::extensions::filters::listener::FilterFuzzWithDataTestCase& input);

private:
  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf, false);
  }

  void connect();
  void disconnect();

  testing::NiceMock<Runtime::MockLoader> runtime_;
  testing::NiceMock<Random::MockRandomGenerator> random_;
  Stats::TestUtil::TestStore stats_store_;
  Api::ApiPtr api_;
  BasicResourceLimitImpl open_connections_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Network::TcpListenSocket> socket_;
  std::vector<Network::ListenSocketFactoryPtr> socket_factories_;
  Network::NopConnectionBalancerImpl connection_balancer_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::ListenerFilterPtr filter_{nullptr};
  Network::MockFilterChainFactory factory_;
  Network::ClientConnectionPtr conn_;
  NiceMock<Network::MockConnectionCallbacks> connection_callbacks_;
  std::string name_;
  const Network::FilterChainSharedPtr filter_chain_;
  const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
  std::unique_ptr<Init::Manager> init_manager_;
  bool connection_established_{};
  const Network::ListenerInfoConstSharedPtr listener_info_;
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
