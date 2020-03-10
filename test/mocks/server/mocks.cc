#include "mocks.h"

#include <string>

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {

MockOptions::MockOptions(const std::string& config_path) : config_path_(config_path) {
  ON_CALL(*this, concurrency()).WillByDefault(ReturnPointee(&concurrency_));
  ON_CALL(*this, configPath()).WillByDefault(ReturnRef(config_path_));
  ON_CALL(*this, configProto()).WillByDefault(ReturnRef(config_proto_));
  ON_CALL(*this, configYaml()).WillByDefault(ReturnRef(config_yaml_));
  ON_CALL(*this, allowUnknownStaticFields()).WillByDefault(Invoke([this] {
    return allow_unknown_static_fields_;
  }));
  ON_CALL(*this, rejectUnknownDynamicFields()).WillByDefault(Invoke([this] {
    return reject_unknown_dynamic_fields_;
  }));
  ON_CALL(*this, adminAddressPath()).WillByDefault(ReturnRef(admin_address_path_));
  ON_CALL(*this, serviceClusterName()).WillByDefault(ReturnRef(service_cluster_name_));
  ON_CALL(*this, serviceNodeName()).WillByDefault(ReturnRef(service_node_name_));
  ON_CALL(*this, serviceZone()).WillByDefault(ReturnRef(service_zone_name_));
  ON_CALL(*this, logLevel()).WillByDefault(Return(log_level_));
  ON_CALL(*this, logPath()).WillByDefault(ReturnRef(log_path_));
  ON_CALL(*this, restartEpoch()).WillByDefault(ReturnPointee(&hot_restart_epoch_));
  ON_CALL(*this, hotRestartDisabled()).WillByDefault(ReturnPointee(&hot_restart_disabled_));
  ON_CALL(*this, signalHandlingEnabled()).WillByDefault(ReturnPointee(&signal_handling_enabled_));
  ON_CALL(*this, mutexTracingEnabled()).WillByDefault(ReturnPointee(&mutex_tracing_enabled_));
  ON_CALL(*this, cpusetThreadsEnabled()).WillByDefault(ReturnPointee(&cpuset_threads_enabled_));
  ON_CALL(*this, disabledExtensions()).WillByDefault(ReturnRef(disabled_extensions_));
  ON_CALL(*this, toCommandLineOptions()).WillByDefault(Invoke([] {
    return std::make_unique<envoy::admin::v3::CommandLineOptions>();
  }));
}
MockOptions::~MockOptions() = default;

MockConfigTracker::MockConfigTracker() {
  ON_CALL(*this, add_(_, _))
      .WillByDefault(Invoke([this](const std::string& key, Cb callback) -> EntryOwner* {
        EXPECT_TRUE(config_tracker_callbacks_.find(key) == config_tracker_callbacks_.end());
        config_tracker_callbacks_[key] = callback;
        return new MockEntryOwner();
      }));
}
MockConfigTracker::~MockConfigTracker() = default;

MockAdmin::MockAdmin() {
  ON_CALL(*this, getConfigTracker()).WillByDefault(testing::ReturnRef(config_tracker_));
}
MockAdmin::~MockAdmin() = default;

MockAdminStream::MockAdminStream() = default;
MockAdminStream::~MockAdminStream() = default;

MockDrainManager::MockDrainManager() {
  ON_CALL(*this, startDrainSequence(_)).WillByDefault(SaveArg<0>(&drain_sequence_completion_));
}
MockDrainManager::~MockDrainManager() = default;

MockWatchDog::MockWatchDog() = default;
MockWatchDog::~MockWatchDog() = default;

MockGuardDog::MockGuardDog() : watch_dog_(new NiceMock<MockWatchDog>()) {
  ON_CALL(*this, createWatchDog(_, _)).WillByDefault(Return(watch_dog_));
}
MockGuardDog::~MockGuardDog() = default;

MockHotRestart::MockHotRestart() : stats_allocator_(*symbol_table_) {
  ON_CALL(*this, logLock()).WillByDefault(ReturnRef(log_lock_));
  ON_CALL(*this, accessLogLock()).WillByDefault(ReturnRef(access_log_lock_));
  ON_CALL(*this, statsAllocator()).WillByDefault(ReturnRef(stats_allocator_));
}
MockHotRestart::~MockHotRestart() = default;

MockOverloadManager::MockOverloadManager() {
  ON_CALL(*this, getThreadLocalOverloadState()).WillByDefault(ReturnRef(overload_state_));
}
MockOverloadManager::~MockOverloadManager() = default;

MockListenerComponentFactory::MockListenerComponentFactory()
    : socket_(std::make_shared<NiceMock<Network::MockListenSocket>>()) {
  ON_CALL(*this, createListenSocket(_, _, _, _))
      .WillByDefault(Invoke([&](Network::Address::InstanceConstSharedPtr,
                                Network::Address::SocketType,
                                const Network::Socket::OptionsSharedPtr& options,
                                const ListenSocketCreationParams&) -> Network::SocketSharedPtr {
        if (!Network::Socket::applyOptions(options, *socket_,
                                           envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
          throw EnvoyException("MockListenerComponentFactory: Setting socket options failed");
        }
        return socket_;
      }));
}
MockListenerComponentFactory::~MockListenerComponentFactory() = default;

MockServerLifecycleNotifier::MockServerLifecycleNotifier() = default;
MockServerLifecycleNotifier::~MockServerLifecycleNotifier() = default;

MockListenerManager::MockListenerManager() = default;
MockListenerManager::~MockListenerManager() = default;

MockWorkerFactory::MockWorkerFactory() = default;
MockWorkerFactory::~MockWorkerFactory() = default;

MockWorker::MockWorker() {
  ON_CALL(*this, addListener(_, _))
      .WillByDefault(
          Invoke([this](Network::ListenerConfig& config, AddListenerCompletion completion) -> void {
            config.listenSocketFactory().getListenSocket();
            EXPECT_EQ(nullptr, add_listener_completion_);
            add_listener_completion_ = completion;
          }));

  ON_CALL(*this, removeListener(_, _))
      .WillByDefault(
          Invoke([this](Network::ListenerConfig&, std::function<void()> completion) -> void {
            EXPECT_EQ(nullptr, remove_listener_completion_);
            remove_listener_completion_ = completion;
          }));

  ON_CALL(*this, stopListener(_, _))
      .WillByDefault(Invoke([](Network::ListenerConfig&, std::function<void()> completion) -> void {
        if (completion != nullptr) {
          completion();
        }
      }));
}
MockWorker::~MockWorker() = default;

MockInstance::MockInstance()
    : secret_manager_(std::make_unique<Secret::SecretManagerImpl>(admin_.getConfigTracker())),
      cluster_manager_(timeSource()), ssl_context_manager_(timeSource()),
      singleton_manager_(new Singleton::ManagerImpl(Thread::threadFactoryForTest())),
      grpc_context_(stats_store_.symbolTable()), http_context_(stats_store_.symbolTable()),
      server_factory_context_(
          std::make_shared<NiceMock<Configuration::MockServerFactoryContext>>()),
      transport_socket_factory_context_(
          std::make_shared<NiceMock<Configuration::MockTransportSocketFactoryContext>>()) {
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, grpcContext()).WillByDefault(ReturnRef(grpc_context_));
  ON_CALL(*this, httpContext()).WillByDefault(ReturnRef(http_context_));
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
  ON_CALL(*this, lifecycleNotifier()).WillByDefault(ReturnRef(lifecycle_notifier_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, drainManager()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, listenerManager()).WillByDefault(ReturnRef(listener_manager_));
  ON_CALL(*this, mutexTracer()).WillByDefault(Return(nullptr));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, overloadManager()).WillByDefault(ReturnRef(overload_manager_));
  ON_CALL(*this, messageValidationContext()).WillByDefault(ReturnRef(validation_context_));
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(*server_factory_context_));
  ON_CALL(*this, transportSocketFactoryContext())
      .WillByDefault(ReturnRef(*transport_socket_factory_context_));
}

MockInstance::~MockInstance() = default;

namespace Configuration {

MockMain::MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill)
    : wd_miss_(wd_miss), wd_megamiss_(wd_megamiss), wd_kill_(wd_kill), wd_multikill_(wd_multikill) {
  ON_CALL(*this, wdMissTimeout()).WillByDefault(Return(wd_miss_));
  ON_CALL(*this, wdMegaMissTimeout()).WillByDefault(Return(wd_megamiss_));
  ON_CALL(*this, wdKillTimeout()).WillByDefault(Return(wd_kill_));
  ON_CALL(*this, wdMultiKillTimeout()).WillByDefault(Return(wd_multikill_));
}

MockMain::~MockMain() = default;

MockServerFactoryContext::MockServerFactoryContext()
    : singleton_manager_(new Singleton::ManagerImpl(Thread::threadFactoryForTest())),
      grpc_context_(scope_.symbolTable()) {
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, drainDecision()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_loader_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, admin()).WillByDefault(ReturnRef(admin_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, timeSource()).WillByDefault(ReturnRef(time_system_));
  ON_CALL(*this, messageValidationContext()).WillByDefault(ReturnRef(validation_context_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
}
MockServerFactoryContext::~MockServerFactoryContext() = default;

MockFactoryContext::MockFactoryContext()
    : singleton_manager_(new Singleton::ManagerImpl(Thread::threadFactoryForTest())),
      grpc_context_(scope_.symbolTable()), http_context_(scope_.symbolTable()) {
  ON_CALL(*this, getServerFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, accessLogManager()).WillByDefault(ReturnRef(access_log_manager_));
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, drainDecision()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, lifecycleNotifier()).WillByDefault(ReturnRef(lifecycle_notifier_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_loader_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, admin()).WillByDefault(ReturnRef(admin_));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(listener_scope_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, timeSource()).WillByDefault(ReturnRef(time_system_));
  ON_CALL(*this, overloadManager()).WillByDefault(ReturnRef(overload_manager_));
  ON_CALL(*this, messageValidationContext()).WillByDefault(ReturnRef(validation_context_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
}

MockFactoryContext::~MockFactoryContext() = default;

MockTransportSocketFactoryContext::MockTransportSocketFactoryContext()
    : secret_manager_(std::make_unique<Secret::SecretManagerImpl>(config_tracker_)) {
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}

MockTransportSocketFactoryContext::~MockTransportSocketFactoryContext() = default;

MockListenerFactoryContext::MockListenerFactoryContext() = default;
MockListenerFactoryContext::~MockListenerFactoryContext() = default;

MockHealthCheckerFactoryContext::MockHealthCheckerFactoryContext() {
  event_logger_ = new NiceMock<Upstream::MockHealthCheckEventLogger>();
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_));
  ON_CALL(*this, eventLogger_()).WillByDefault(Return(event_logger_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
}

MockHealthCheckerFactoryContext::~MockHealthCheckerFactoryContext() = default;

MockFilterChainFactoryContext::MockFilterChainFactoryContext() = default;
MockFilterChainFactoryContext::~MockFilterChainFactoryContext() = default;

MockTracerFactoryContext::MockTracerFactoryContext() {
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}

MockTracerFactoryContext::~MockTracerFactoryContext() = default;
} // namespace Configuration
} // namespace Server
} // namespace Envoy
