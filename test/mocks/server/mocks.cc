#include "mocks.h"

#include <string>

#include "common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {

MockOptions::MockOptions(const std::string& config_path) : config_path_(config_path) {
  ON_CALL(*this, concurrency()).WillByDefault(ReturnPointee(&concurrency_));
  ON_CALL(*this, configPath()).WillByDefault(ReturnRef(config_path_));
  ON_CALL(*this, configYaml()).WillByDefault(ReturnRef(config_yaml_));
  ON_CALL(*this, v2ConfigOnly()).WillByDefault(Invoke([this] { return v2_config_only_; }));
  ON_CALL(*this, adminAddressPath()).WillByDefault(ReturnRef(admin_address_path_));
  ON_CALL(*this, serviceClusterName()).WillByDefault(ReturnRef(service_cluster_name_));
  ON_CALL(*this, serviceNodeName()).WillByDefault(ReturnRef(service_node_name_));
  ON_CALL(*this, serviceZone()).WillByDefault(ReturnRef(service_zone_name_));
  ON_CALL(*this, logLevel()).WillByDefault(Return(log_level_));
  ON_CALL(*this, logPath()).WillByDefault(ReturnRef(log_path_));
  ON_CALL(*this, maxStats()).WillByDefault(Return(1000));
  ON_CALL(*this, statsOptions()).WillByDefault(ReturnRef(stats_options_));
  ON_CALL(*this, restartEpoch()).WillByDefault(ReturnPointee(&hot_restart_epoch_));
  ON_CALL(*this, hotRestartDisabled()).WillByDefault(ReturnPointee(&hot_restart_disabled_));
}
MockOptions::~MockOptions() {}

MockConfigTracker::MockConfigTracker() {
  ON_CALL(*this, add_(_, _))
      .WillByDefault(Invoke([this](const std::string& key, Cb callback) -> EntryOwner* {
        EXPECT_TRUE(config_tracker_callbacks_.find(key) == config_tracker_callbacks_.end());
        config_tracker_callbacks_[key] = callback;
        return new MockEntryOwner();
      }));
}
MockConfigTracker::~MockConfigTracker() {}

MockAdmin::MockAdmin() {
  ON_CALL(*this, getConfigTracker()).WillByDefault(testing::ReturnRef(config_tracker_));
}
MockAdmin::~MockAdmin() {}

MockDrainManager::MockDrainManager() {
  ON_CALL(*this, startDrainSequence(_)).WillByDefault(SaveArg<0>(&drain_sequence_completion_));
}
MockDrainManager::~MockDrainManager() {}

MockWatchDog::MockWatchDog() {}
MockWatchDog::~MockWatchDog() {}

MockGuardDog::MockGuardDog() : watch_dog_(new NiceMock<MockWatchDog>()) {
  ON_CALL(*this, createWatchDog(_)).WillByDefault(Return(watch_dog_));
}
MockGuardDog::~MockGuardDog() {}

MockHotRestart::MockHotRestart() {
  ON_CALL(*this, logLock()).WillByDefault(ReturnRef(log_lock_));
  ON_CALL(*this, accessLogLock()).WillByDefault(ReturnRef(access_log_lock_));
  ON_CALL(*this, statsAllocator()).WillByDefault(ReturnRef(stats_allocator_));
}
MockHotRestart::~MockHotRestart() {}

MockListenerComponentFactory::MockListenerComponentFactory()
    : socket_(std::make_shared<NiceMock<Network::MockListenSocket>>()) {
  ON_CALL(*this, createListenSocket(_, _, _))
      .WillByDefault(Invoke([&](Network::Address::InstanceConstSharedPtr,
                                const Network::Socket::OptionsSharedPtr& options,
                                bool) -> Network::SocketSharedPtr {
        if (!Network::Socket::applyOptions(options, *socket_,
                                           envoy::api::v2::core::SocketOption::STATE_PREBIND)) {
          throw EnvoyException("MockListenerComponentFactory: Setting socket options failed");
        }
        return socket_;
      }));
}
MockListenerComponentFactory::~MockListenerComponentFactory() {}

MockListenerManager::MockListenerManager() {}
MockListenerManager::~MockListenerManager() {}

MockWorkerFactory::MockWorkerFactory() {}

MockWorkerFactory::~MockWorkerFactory() {}

MockWorker::MockWorker() {
  ON_CALL(*this, addListener(_, _))
      .WillByDefault(
          Invoke([this](Network::ListenerConfig&, AddListenerCompletion completion) -> void {
            EXPECT_EQ(nullptr, add_listener_completion_);
            add_listener_completion_ = completion;
          }));

  ON_CALL(*this, removeListener(_, _))
      .WillByDefault(
          Invoke([this](Network::ListenerConfig&, std::function<void()> completion) -> void {
            EXPECT_EQ(nullptr, remove_listener_completion_);
            remove_listener_completion_ = completion;
          }));
}
MockWorker::~MockWorker() {}

MockInstance::MockInstance()
    : secret_manager_(new Secret::SecretManagerImpl()), ssl_context_manager_(runtime_loader_),
      singleton_manager_(new Singleton::ManagerImpl()) {
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, httpTracer()).WillByDefault(ReturnRef(http_tracer_));
  ON_CALL(*this, dnsResolver()).WillByDefault(Return(dns_resolver_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, admin()).WillByDefault(ReturnRef(admin_));
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
  ON_CALL(*this, accessLogManager()).WillByDefault(ReturnRef(access_log_manager_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_loader_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, hotRestart()).WillByDefault(ReturnRef(hot_restart_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, drainManager()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, listenerManager()).WillByDefault(ReturnRef(listener_manager_));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, overloadManager()).WillByDefault(ReturnRef(overload_manager_));
  ON_CALL(*this, timeSource()).WillByDefault(ReturnRef(test_time_.timeSource()));
}

MockInstance::~MockInstance() {}

namespace Configuration {

MockMain::MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill)
    : wd_miss_(wd_miss), wd_megamiss_(wd_megamiss), wd_kill_(wd_kill), wd_multikill_(wd_multikill) {
  ON_CALL(*this, wdMissTimeout()).WillByDefault(Return(wd_miss_));
  ON_CALL(*this, wdMegaMissTimeout()).WillByDefault(Return(wd_megamiss_));
  ON_CALL(*this, wdKillTimeout()).WillByDefault(Return(wd_kill_));
  ON_CALL(*this, wdMultiKillTimeout()).WillByDefault(Return(wd_multikill_));
}

MockFactoryContext::MockFactoryContext()
    : singleton_manager_(new Singleton::ManagerImpl()),
      time_source_(system_time_source_, monotonic_time_source_) {
  ON_CALL(*this, accessLogManager()).WillByDefault(ReturnRef(access_log_manager_));
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, drainDecision()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, httpTracer()).WillByDefault(ReturnRef(http_tracer_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_loader_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, admin()).WillByDefault(ReturnRef(admin_));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(listener_scope_));
  ON_CALL(*this, systemTimeSource()).WillByDefault(ReturnRef(system_time_source_));
  ON_CALL(*this, timeSource()).WillByDefault(ReturnRef(time_source_));
}

MockFactoryContext::~MockFactoryContext() {}

MockTransportSocketFactoryContext::MockTransportSocketFactoryContext()
    : secret_manager_(new Secret::SecretManagerImpl()) {}

MockTransportSocketFactoryContext::~MockTransportSocketFactoryContext() {}

MockListenerFactoryContext::MockListenerFactoryContext() {}
MockListenerFactoryContext::~MockListenerFactoryContext() {}

MockHealthCheckerFactoryContext::MockHealthCheckerFactoryContext() {
  event_logger_ = new NiceMock<Upstream::MockHealthCheckEventLogger>();
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_));
  ON_CALL(*this, eventLogger_()).WillByDefault(Return(event_logger_));
}

MockHealthCheckerFactoryContext::~MockHealthCheckerFactoryContext() {}

MockAdminStream::MockAdminStream() {}
MockAdminStream::~MockAdminStream() {}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
