#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/mutex_tracer.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/admin.h"
#include "envoy/server/configuration.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/server/worker.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread/thread.h"

#include "common/grpc/context_impl.h"
#include "common/http/context_impl.h"
#include "common/secret/secret_manager_impl.h"
#include "common/stats/fake_symbol_table_impl.h"

#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/test_time_system.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

namespace Configuration {
class MockServerFactoryContext;
}

class MockOptions : public Options {
public:
  MockOptions() : MockOptions(std::string()) {}
  MockOptions(const std::string& config_path);
  ~MockOptions() override;

  MOCK_CONST_METHOD0(baseId, uint64_t());
  MOCK_CONST_METHOD0(concurrency, uint32_t());
  MOCK_CONST_METHOD0(configPath, const std::string&());
  MOCK_CONST_METHOD0(configProto, const envoy::config::bootstrap::v2::Bootstrap&());
  MOCK_CONST_METHOD0(configYaml, const std::string&());
  MOCK_CONST_METHOD0(allowUnknownStaticFields, bool());
  MOCK_CONST_METHOD0(rejectUnknownDynamicFields, bool());
  MOCK_CONST_METHOD0(adminAddressPath, const std::string&());
  MOCK_CONST_METHOD0(localAddressIpVersion, Network::Address::IpVersion());
  MOCK_CONST_METHOD0(drainTime, std::chrono::seconds());
  MOCK_CONST_METHOD0(logLevel, spdlog::level::level_enum());
  MOCK_CONST_METHOD0(componentLogLevels,
                     const std::vector<std::pair<std::string, spdlog::level::level_enum>>&());
  MOCK_CONST_METHOD0(logFormat, const std::string&());
  MOCK_CONST_METHOD0(logPath, const std::string&());
  MOCK_CONST_METHOD0(parentShutdownTime, std::chrono::seconds());
  MOCK_CONST_METHOD0(restartEpoch, uint64_t());
  MOCK_CONST_METHOD0(fileFlushIntervalMsec, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(mode, Mode());
  MOCK_CONST_METHOD0(serviceClusterName, const std::string&());
  MOCK_CONST_METHOD0(serviceNodeName, const std::string&());
  MOCK_CONST_METHOD0(serviceZone, const std::string&());
  MOCK_CONST_METHOD0(hotRestartDisabled, bool());
  MOCK_CONST_METHOD0(signalHandlingEnabled, bool());
  MOCK_CONST_METHOD0(mutexTracingEnabled, bool());
  MOCK_CONST_METHOD0(libeventBufferEnabled, bool());
  MOCK_CONST_METHOD0(fakeSymbolTableEnabled, bool());
  MOCK_CONST_METHOD0(cpusetThreadsEnabled, bool());
  MOCK_CONST_METHOD0(toCommandLineOptions, Server::CommandLineOptionsPtr());

  std::string config_path_;
  envoy::config::bootstrap::v2::Bootstrap config_proto_;
  std::string config_yaml_;
  bool allow_unknown_static_fields_{};
  bool reject_unknown_dynamic_fields_{};
  std::string admin_address_path_;
  std::string service_cluster_name_;
  std::string service_node_name_;
  std::string service_zone_name_;
  spdlog::level::level_enum log_level_{spdlog::level::trace};
  std::string log_path_;
  uint32_t concurrency_{1};
  uint64_t hot_restart_epoch_{};
  bool hot_restart_disabled_{};
  bool signal_handling_enabled_{true};
  bool mutex_tracing_enabled_{};
  bool cpuset_threads_enabled_{};
};

class MockConfigTracker : public ConfigTracker {
public:
  MockConfigTracker();
  ~MockConfigTracker() override;

  struct MockEntryOwner : public EntryOwner {};

  MOCK_METHOD2(add_, EntryOwner*(std::string, Cb));

  // Server::ConfigTracker
  MOCK_CONST_METHOD0(getCallbacksMap, const CbsMap&());
  EntryOwnerPtr add(const std::string& key, Cb callback) override {
    return EntryOwnerPtr{add_(key, std::move(callback))};
  }

  std::unordered_map<std::string, Cb> config_tracker_callbacks_;
};

class MockAdmin : public Admin {
public:
  MockAdmin();
  ~MockAdmin() override;

  // Server::Admin
  MOCK_METHOD5(addHandler, bool(const std::string& prefix, const std::string& help_text,
                                HandlerCb callback, bool removable, bool mutates_server_state));
  MOCK_METHOD1(removeHandler, bool(const std::string& prefix));
  MOCK_METHOD0(socket, Network::Socket&());
  MOCK_METHOD0(getConfigTracker, ConfigTracker&());
  MOCK_METHOD5(startHttpListener,
               void(const std::string& access_log_path, const std::string& address_out_path,
                    Network::Address::InstanceConstSharedPtr address,
                    const Network::Socket::OptionsSharedPtr& socket_options,
                    Stats::ScopePtr&& listener_scope));
  MOCK_METHOD4(request, Http::Code(absl::string_view path_and_query, absl::string_view method,
                                   Http::HeaderMap& response_headers, std::string& body));
  MOCK_METHOD1(addListenerToHandler, void(Network::ConnectionHandler* handler));

  NiceMock<MockConfigTracker> config_tracker_;
};

class MockAdminStream : public AdminStream {
public:
  MockAdminStream();
  ~MockAdminStream() override;

  MOCK_METHOD1(setEndStreamOnComplete, void(bool));
  MOCK_METHOD1(addOnDestroyCallback, void(std::function<void()>));
  MOCK_CONST_METHOD0(getRequestBody, const Buffer::Instance*());
  MOCK_CONST_METHOD0(getRequestHeaders, Http::HeaderMap&());
  MOCK_CONST_METHOD0(getDecoderFilterCallbacks,
                     NiceMock<Http::MockStreamDecoderFilterCallbacks>&());
};

class MockDrainManager : public DrainManager {
public:
  MockDrainManager();
  ~MockDrainManager() override;

  // Server::DrainManager
  MOCK_CONST_METHOD0(drainClose, bool());
  MOCK_METHOD1(startDrainSequence, void(std::function<void()> completion));
  MOCK_METHOD0(startParentShutdownSequence, void());

  std::function<void()> drain_sequence_completion_;
};

class MockWatchDog : public WatchDog {
public:
  MockWatchDog();
  ~MockWatchDog() override;

  // Server::WatchDog
  MOCK_METHOD1(startWatchdog, void(Event::Dispatcher& dispatcher));
  MOCK_METHOD0(touch, void());
  MOCK_CONST_METHOD0(threadId, Thread::ThreadId());
  MOCK_CONST_METHOD0(lastTouchTime, MonotonicTime());
};

class MockGuardDog : public GuardDog {
public:
  MockGuardDog();
  ~MockGuardDog() override;

  // Server::GuardDog
  MOCK_METHOD2(createWatchDog,
               WatchDogSharedPtr(Thread::ThreadId thread_id, const std::string& thread_name));
  MOCK_METHOD1(stopWatching, void(WatchDogSharedPtr wd));

  std::shared_ptr<MockWatchDog> watch_dog_;
};

class MockHotRestart : public HotRestart {
public:
  MockHotRestart();
  ~MockHotRestart() override;

  // Server::HotRestart
  MOCK_METHOD0(drainParentListeners, void());
  MOCK_METHOD1(duplicateParentListenSocket, int(const std::string& address));
  MOCK_METHOD0(getParentStats, std::unique_ptr<envoy::HotRestartMessage>());
  MOCK_METHOD2(initialize, void(Event::Dispatcher& dispatcher, Server::Instance& server));
  MOCK_METHOD1(sendParentAdminShutdownRequest, void(time_t& original_start_time));
  MOCK_METHOD0(sendParentTerminateRequest, void());
  MOCK_METHOD1(mergeParentStatsIfAny, ServerStatsFromParent(Stats::StoreRoot& stats_store));
  MOCK_METHOD0(shutdown, void());
  MOCK_METHOD0(version, std::string());
  MOCK_METHOD0(logLock, Thread::BasicLockable&());
  MOCK_METHOD0(accessLogLock, Thread::BasicLockable&());
  MOCK_METHOD0(statsAllocator, Stats::Allocator&());

private:
  Stats::TestSymbolTable symbol_table_;
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable access_log_lock_;
  Stats::AllocatorImpl stats_allocator_;
};

class MockListenerComponentFactory : public ListenerComponentFactory {
public:
  MockListenerComponentFactory();
  ~MockListenerComponentFactory() override;

  DrainManagerPtr createDrainManager(envoy::api::v2::Listener::DrainType drain_type) override {
    return DrainManagerPtr{createDrainManager_(drain_type)};
  }
  LdsApiPtr createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) override {
    return LdsApiPtr{createLdsApi_(lds_config)};
  }

  MOCK_METHOD1(createLdsApi_, LdsApi*(const envoy::api::v2::core::ConfigSource& lds_config));
  MOCK_METHOD2(createNetworkFilterFactoryList,
               std::vector<Network::FilterFactoryCb>(
                   const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
                   Configuration::FactoryContext& context));
  MOCK_METHOD2(createListenerFilterFactoryList,
               std::vector<Network::ListenerFilterFactoryCb>(
                   const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>&,
                   Configuration::ListenerFactoryContext& context));
  MOCK_METHOD2(createUdpListenerFilterFactoryList,
               std::vector<Network::UdpListenerFilterFactoryCb>(
                   const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>&,
                   Configuration::ListenerFactoryContext& context));
  MOCK_METHOD4(createListenSocket,
               Network::SocketSharedPtr(Network::Address::InstanceConstSharedPtr address,
                                        Network::Address::SocketType socket_type,
                                        const Network::Socket::OptionsSharedPtr& options,
                                        bool bind_to_port));
  MOCK_METHOD1(createDrainManager_, DrainManager*(envoy::api::v2::Listener::DrainType drain_type));
  MOCK_METHOD0(nextListenerTag, uint64_t());

  std::shared_ptr<Network::MockListenSocket> socket_;
};

class MockListenerManager : public ListenerManager {
public:
  MockListenerManager();
  ~MockListenerManager() override;

  MOCK_METHOD3(addOrUpdateListener, bool(const envoy::api::v2::Listener& config,
                                         const std::string& version_info, bool modifiable));
  MOCK_METHOD1(createLdsApi, void(const envoy::api::v2::core::ConfigSource& lds_config));
  MOCK_METHOD0(listeners, std::vector<std::reference_wrapper<Network::ListenerConfig>>());
  MOCK_METHOD0(numConnections, uint64_t());
  MOCK_METHOD1(removeListener, bool(const std::string& listener_name));
  MOCK_METHOD1(startWorkers, void(GuardDog& guard_dog));
  MOCK_METHOD1(stopListeners, void(StopListenersType listeners_type));
  MOCK_METHOD0(stopWorkers, void());
};

class MockServerLifecycleNotifier : public ServerLifecycleNotifier {
public:
  MockServerLifecycleNotifier();
  ~MockServerLifecycleNotifier() override;

  MOCK_METHOD2(registerCallback, ServerLifecycleNotifier::HandlePtr(Stage, StageCallback));
  MOCK_METHOD2(registerCallback,
               ServerLifecycleNotifier::HandlePtr(Stage, StageCallbackWithCompletion));
};

class MockWorkerFactory : public WorkerFactory {
public:
  MockWorkerFactory();
  ~MockWorkerFactory() override;

  // Server::WorkerFactory
  WorkerPtr createWorker(OverloadManager&, const std::string&) override {
    return WorkerPtr{createWorker_()};
  }

  MOCK_METHOD0(createWorker_, Worker*());
};

class MockWorker : public Worker {
public:
  MockWorker();
  ~MockWorker() override;

  void callAddCompletion(bool success) {
    EXPECT_NE(nullptr, add_listener_completion_);
    add_listener_completion_(success);
    add_listener_completion_ = nullptr;
  }

  void callRemovalCompletion() {
    EXPECT_NE(nullptr, remove_listener_completion_);
    remove_listener_completion_();
    remove_listener_completion_ = nullptr;
  }

  // Server::Worker
  MOCK_METHOD2(addListener,
               void(Network::ListenerConfig& listener, AddListenerCompletion completion));
  MOCK_METHOD0(numConnections, uint64_t());
  MOCK_METHOD2(removeListener,
               void(Network::ListenerConfig& listener, std::function<void()> completion));
  MOCK_METHOD1(start, void(GuardDog& guard_dog));
  MOCK_METHOD2(initializeStats, void(Stats::Scope& scope, const std::string& prefix));
  MOCK_METHOD0(stop, void());
  MOCK_METHOD1(stopListener, void(Network::ListenerConfig& listener));
  MOCK_METHOD0(stopListeners, void());

  AddListenerCompletion add_listener_completion_;
  std::function<void()> remove_listener_completion_;
};

class MockOverloadManager : public OverloadManager {
public:
  MockOverloadManager();
  ~MockOverloadManager() override;

  // OverloadManager
  MOCK_METHOD0(start, void());
  MOCK_METHOD3(registerForAction, bool(const std::string& action, Event::Dispatcher& dispatcher,
                                       OverloadActionCb callback));
  MOCK_METHOD0(getThreadLocalOverloadState, ThreadLocalOverloadState&());

  ThreadLocalOverloadState overload_state_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_.get()); }

  MOCK_METHOD0(admin, Admin&());
  MOCK_METHOD0(api, Api::Api&());
  MOCK_METHOD0(clusterManager, Upstream::ClusterManager&());
  MOCK_METHOD0(sslContextManager, Ssl::ContextManager&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(dnsResolver, Network::DnsResolverSharedPtr());
  MOCK_METHOD0(drainListeners, void());
  MOCK_METHOD0(drainManager, DrainManager&());
  MOCK_METHOD0(accessLogManager, AccessLog::AccessLogManager&());
  MOCK_METHOD1(failHealthcheck, void(bool fail));
  MOCK_METHOD1(exportStatsToChild, void(envoy::HotRestartMessage::Reply::Stats*));
  MOCK_METHOD0(healthCheckFailed, bool());
  MOCK_METHOD0(hotRestart, HotRestart&());
  MOCK_METHOD0(initManager, Init::Manager&());
  MOCK_METHOD0(lifecycleNotifier, ServerLifecycleNotifier&());
  MOCK_METHOD0(listenerManager, ListenerManager&());
  MOCK_METHOD0(mutexTracer, Envoy::MutexTracer*());
  MOCK_METHOD0(options, const Options&());
  MOCK_METHOD0(overloadManager, OverloadManager&());
  MOCK_METHOD0(random, Runtime::RandomGenerator&());
  MOCK_METHOD0(runtime, Runtime::Loader&());
  MOCK_METHOD0(shutdown, void());
  MOCK_METHOD0(isShutdown, bool());
  MOCK_METHOD0(shutdownAdmin, void());
  MOCK_METHOD0(singletonManager, Singleton::Manager&());
  MOCK_METHOD0(startTimeCurrentEpoch, time_t());
  MOCK_METHOD0(startTimeFirstEpoch, time_t());
  MOCK_METHOD0(stats, Stats::Store&());
  MOCK_METHOD0(grpcContext, Grpc::Context&());
  MOCK_METHOD0(httpContext, Http::Context&());
  MOCK_METHOD0(processContext, absl::optional<std::reference_wrapper<ProcessContext>>());
  MOCK_METHOD0(threadLocal, ThreadLocal::Instance&());
  MOCK_CONST_METHOD0(localInfo, const LocalInfo::LocalInfo&());
  MOCK_CONST_METHOD0(statsFlushInterval, std::chrono::milliseconds());
  MOCK_METHOD0(messageValidationContext, ProtobufMessage::ValidationContext&());
  MOCK_METHOD0(serverFactoryContext, Configuration::ServerFactoryContext&());

  TimeSource& timeSource() override { return time_system_; }

  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  std::shared_ptr<testing::NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new testing::NiceMock<Network::MockDnsResolver>()};
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockAdmin> admin_;
  Event::GlobalTimeSystem time_system_;
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Thread::MutexBasicLockable access_log_lock_;
  testing::NiceMock<Runtime::MockLoader> runtime_loader_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<MockHotRestart> hot_restart_;
  testing::NiceMock<MockOptions> options_;
  testing::NiceMock<Runtime::MockRandomGenerator> random_;
  testing::NiceMock<MockServerLifecycleNotifier> lifecycle_notifier_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockListenerManager> listener_manager_;
  testing::NiceMock<MockOverloadManager> overload_manager_;
  Singleton::ManagerPtr singleton_manager_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  std::shared_ptr<testing::NiceMock<Configuration::MockServerFactoryContext>>
      server_factory_context_;
};

namespace Configuration {

class MockMain : public Main {
public:
  MockMain() : MockMain(0, 0, 0, 0) {}
  MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill);
  ~MockMain() override;

  MOCK_METHOD0(clusterManager, Upstream::ClusterManager*());
  MOCK_METHOD0(httpTracer, Tracing::HttpTracer&());
  MOCK_METHOD0(statsSinks, std::list<Stats::SinkPtr>&());
  MOCK_CONST_METHOD0(statsFlushInterval, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(wdMissTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(wdMegaMissTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(wdKillTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(wdMultiKillTimeout, std::chrono::milliseconds());

  std::chrono::milliseconds wd_miss_;
  std::chrono::milliseconds wd_megamiss_;
  std::chrono::milliseconds wd_kill_;
  std::chrono::milliseconds wd_multikill_;
};

class MockServerFactoryContext : public virtual ServerFactoryContext {
public:
  MockServerFactoryContext();
  ~MockServerFactoryContext() override;

  MOCK_METHOD0(clusterManager, Upstream::ClusterManager&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(drainDecision, const Network::DrainDecision&());
  MOCK_CONST_METHOD0(localInfo, const LocalInfo::LocalInfo&());
  MOCK_METHOD0(random, Envoy::Runtime::RandomGenerator&());
  MOCK_METHOD0(runtime, Envoy::Runtime::Loader&());
  MOCK_METHOD0(scope, Stats::Scope&());
  MOCK_METHOD0(singletonManager, Singleton::Manager&());
  MOCK_METHOD0(threadLocal, ThreadLocal::Instance&());
  MOCK_METHOD0(admin, Server::Admin&());
  MOCK_METHOD0(timeSource, TimeSource&());
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  MOCK_METHOD0(messageValidationVisitor, ProtobufMessage::ValidationVisitor&());
  MOCK_METHOD0(api, Api::Api&());

  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Envoy::Runtime::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> scope_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Api::MockApi> api_;
};

class MockFactoryContext : public virtual FactoryContext {
public:
  MockFactoryContext();
  ~MockFactoryContext() override;

  MOCK_CONST_METHOD0(getServerFactoryContext, ServerFactoryContext&());
  MOCK_METHOD0(accessLogManager, AccessLog::AccessLogManager&());
  MOCK_METHOD0(clusterManager, Upstream::ClusterManager&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(drainDecision, const Network::DrainDecision&());
  MOCK_METHOD0(healthCheckFailed, bool());
  MOCK_METHOD0(httpTracer, Tracing::HttpTracer&());
  MOCK_METHOD0(initManager, Init::Manager&());
  MOCK_METHOD0(lifecycleNotifier, ServerLifecycleNotifier&());
  MOCK_METHOD0(random, Envoy::Runtime::RandomGenerator&());
  MOCK_METHOD0(runtime, Envoy::Runtime::Loader&());
  MOCK_METHOD0(scope, Stats::Scope&());
  MOCK_METHOD0(singletonManager, Singleton::Manager&());
  MOCK_METHOD0(overloadManager, OverloadManager&());
  MOCK_METHOD0(threadLocal, ThreadLocal::Instance&());
  MOCK_METHOD0(admin, Server::Admin&());
  MOCK_METHOD0(listenerScope, Stats::Scope&());
  MOCK_CONST_METHOD0(localInfo, const LocalInfo::LocalInfo&());
  MOCK_CONST_METHOD0(listenerMetadata, const envoy::api::v2::core::Metadata&());
  MOCK_CONST_METHOD0(direction, envoy::api::v2::core::TrafficDirection());
  MOCK_METHOD0(timeSource, TimeSource&());
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }
  MOCK_METHOD0(processContext, OptProcessContextRef());
  MOCK_METHOD0(messageValidationVisitor, ProtobufMessage::ValidationVisitor&());
  MOCK_METHOD0(api, Api::Api&());

  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<Tracing::MockHttpTracer> http_tracer_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockServerLifecycleNotifier> lifecycle_notifier_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Envoy::Runtime::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> scope_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Stats::IsolatedStoreImpl listener_scope_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<MockOverloadManager> overload_manager_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  testing::NiceMock<Api::MockApi> api_;
};

class MockTransportSocketFactoryContext : public TransportSocketFactoryContext {
public:
  MockTransportSocketFactoryContext();
  ~MockTransportSocketFactoryContext() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_.get()); }

  MOCK_METHOD0(admin, Server::Admin&());
  MOCK_METHOD0(sslContextManager, Ssl::ContextManager&());
  MOCK_CONST_METHOD0(statsScope, Stats::Scope&());
  MOCK_METHOD0(clusterManager, Upstream::ClusterManager&());
  MOCK_METHOD0(localInfo, const LocalInfo::LocalInfo&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(random, Envoy::Runtime::RandomGenerator&());
  MOCK_METHOD0(stats, Stats::Store&());
  MOCK_METHOD1(setInitManager, void(Init::Manager&));
  MOCK_METHOD0(initManager, Init::Manager*());
  MOCK_METHOD0(singletonManager, Singleton::Manager&());
  MOCK_METHOD0(threadLocal, ThreadLocal::SlotAllocator&());
  MOCK_METHOD0(messageValidationVisitor, ProtobufMessage::ValidationVisitor&());
  MOCK_METHOD0(api, Api::Api&());

  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockConfigTracker> config_tracker_;
  std::unique_ptr<Secret::SecretManager> secret_manager_;
};

class MockListenerFactoryContext : public MockFactoryContext, public ListenerFactoryContext {
public:
  MockListenerFactoryContext();
  ~MockListenerFactoryContext() override;

  const Network::ListenerConfig& listenerConfig() const override { return listener_config_; }
  MOCK_CONST_METHOD0(listenerConfig_, const Network::ListenerConfig&());

  Network::MockListenerConfig listener_config_;
};

class MockHealthCheckerFactoryContext : public virtual HealthCheckerFactoryContext {
public:
  MockHealthCheckerFactoryContext();
  ~MockHealthCheckerFactoryContext() override;

  MOCK_METHOD0(cluster, Upstream::Cluster&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(random, Envoy::Runtime::RandomGenerator&());
  MOCK_METHOD0(runtime, Envoy::Runtime::Loader&());
  MOCK_METHOD0(eventLogger_, Upstream::HealthCheckEventLogger*());
  MOCK_METHOD0(messageValidationVisitor, ProtobufMessage::ValidationVisitor&());
  MOCK_METHOD0(api, Api::Api&());
  Upstream::HealthCheckEventLoggerPtr eventLogger() override {
    return Upstream::HealthCheckEventLoggerPtr(eventLogger_());
  }

  testing::NiceMock<Upstream::MockClusterMockPrioritySet> cluster_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<Envoy::Runtime::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_;
  testing::NiceMock<Envoy::Upstream::MockHealthCheckEventLogger>* event_logger_{};
  testing::NiceMock<Envoy::Api::MockApi> api_{};
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
