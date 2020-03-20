#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/mutex_tracer.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/admin.h"
#include "envoy/server/configuration.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/tracer_config.h"
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
class MockTransportSocketFactoryContext;
} // namespace Configuration

class MockOptions : public Options {
public:
  MockOptions() : MockOptions(std::string()) {}
  MockOptions(const std::string& config_path);
  ~MockOptions() override;

  MOCK_METHOD(uint64_t, baseId, (), (const));
  MOCK_METHOD(uint32_t, concurrency, (), (const));
  MOCK_METHOD(const std::string&, configPath, (), (const));
  MOCK_METHOD(const envoy::config::bootstrap::v3::Bootstrap&, configProto, (), (const));
  MOCK_METHOD(const std::string&, configYaml, (), (const));
  MOCK_METHOD(bool, allowUnknownStaticFields, (), (const));
  MOCK_METHOD(bool, rejectUnknownDynamicFields, (), (const));
  MOCK_METHOD(const std::string&, adminAddressPath, (), (const));
  MOCK_METHOD(Network::Address::IpVersion, localAddressIpVersion, (), (const));
  MOCK_METHOD(std::chrono::seconds, drainTime, (), (const));
  MOCK_METHOD(spdlog::level::level_enum, logLevel, (), (const));
  MOCK_METHOD((const std::vector<std::pair<std::string, spdlog::level::level_enum>>&),
              componentLogLevels, (), (const));
  MOCK_METHOD(const std::string&, logFormat, (), (const));
  MOCK_METHOD(bool, logFormatEscaped, (), (const));
  MOCK_METHOD(const std::string&, logPath, (), (const));
  MOCK_METHOD(std::chrono::seconds, parentShutdownTime, (), (const));
  MOCK_METHOD(uint64_t, restartEpoch, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, fileFlushIntervalMsec, (), (const));
  MOCK_METHOD(Mode, mode, (), (const));
  MOCK_METHOD(const std::string&, serviceClusterName, (), (const));
  MOCK_METHOD(const std::string&, serviceNodeName, (), (const));
  MOCK_METHOD(const std::string&, serviceZone, (), (const));
  MOCK_METHOD(bool, hotRestartDisabled, (), (const));
  MOCK_METHOD(bool, signalHandlingEnabled, (), (const));
  MOCK_METHOD(bool, mutexTracingEnabled, (), (const));
  MOCK_METHOD(bool, fakeSymbolTableEnabled, (), (const));
  MOCK_METHOD(bool, cpusetThreadsEnabled, (), (const));
  MOCK_METHOD(const std::vector<std::string>&, disabledExtensions, (), (const));
  MOCK_METHOD(Server::CommandLineOptionsPtr, toCommandLineOptions, (), (const));

  std::string config_path_;
  envoy::config::bootstrap::v3::Bootstrap config_proto_;
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
  std::vector<std::string> disabled_extensions_;
};

class MockConfigTracker : public ConfigTracker {
public:
  MockConfigTracker();
  ~MockConfigTracker() override;

  struct MockEntryOwner : public EntryOwner {};

  MOCK_METHOD(EntryOwner*, add_, (std::string, Cb));

  // Server::ConfigTracker
  MOCK_METHOD(const CbsMap&, getCallbacksMap, (), (const));
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
  MOCK_METHOD(bool, addHandler,
              (const std::string& prefix, const std::string& help_text, HandlerCb callback,
               bool removable, bool mutates_server_state));
  MOCK_METHOD(bool, removeHandler, (const std::string& prefix));
  MOCK_METHOD(Network::Socket&, socket, ());
  MOCK_METHOD(ConfigTracker&, getConfigTracker, ());
  MOCK_METHOD(void, startHttpListener,
              (const std::string& access_log_path, const std::string& address_out_path,
               Network::Address::InstanceConstSharedPtr address,
               const Network::Socket::OptionsSharedPtr& socket_options,
               Stats::ScopePtr&& listener_scope));
  MOCK_METHOD(Http::Code, request,
              (absl::string_view path_and_query, absl::string_view method,
               Http::ResponseHeaderMap& response_headers, std::string& body));
  MOCK_METHOD(void, addListenerToHandler, (Network::ConnectionHandler * handler));

  NiceMock<MockConfigTracker> config_tracker_;
};

class MockAdminStream : public AdminStream {
public:
  MockAdminStream();
  ~MockAdminStream() override;

  MOCK_METHOD(void, setEndStreamOnComplete, (bool));
  MOCK_METHOD(void, addOnDestroyCallback, (std::function<void()>));
  MOCK_METHOD(const Buffer::Instance*, getRequestBody, (), (const));
  MOCK_METHOD(Http::RequestHeaderMap&, getRequestHeaders, (), (const));
  MOCK_METHOD(NiceMock<Http::MockStreamDecoderFilterCallbacks>&, getDecoderFilterCallbacks, (),
              (const));
};

class MockDrainManager : public DrainManager {
public:
  MockDrainManager();
  ~MockDrainManager() override;

  // Server::DrainManager
  MOCK_METHOD(bool, drainClose, (), (const));
  MOCK_METHOD(void, startDrainSequence, (std::function<void()> completion));
  MOCK_METHOD(void, startParentShutdownSequence, ());

  std::function<void()> drain_sequence_completion_;
};

class MockWatchDog : public WatchDog {
public:
  MockWatchDog();
  ~MockWatchDog() override;

  // Server::WatchDog
  MOCK_METHOD(void, startWatchdog, (Event::Dispatcher & dispatcher));
  MOCK_METHOD(void, touch, ());
  MOCK_METHOD(Thread::ThreadId, threadId, (), (const));
  MOCK_METHOD(MonotonicTime, lastTouchTime, (), (const));
};

class MockGuardDog : public GuardDog {
public:
  MockGuardDog();
  ~MockGuardDog() override;

  // Server::GuardDog
  MOCK_METHOD(WatchDogSharedPtr, createWatchDog,
              (Thread::ThreadId thread_id, const std::string& thread_name));
  MOCK_METHOD(void, stopWatching, (WatchDogSharedPtr wd));

  std::shared_ptr<MockWatchDog> watch_dog_;
};

class MockHotRestart : public HotRestart {
public:
  MockHotRestart();
  ~MockHotRestart() override;

  // Server::HotRestart
  MOCK_METHOD(void, drainParentListeners, ());
  MOCK_METHOD(int, duplicateParentListenSocket, (const std::string& address));
  MOCK_METHOD(std::unique_ptr<envoy::HotRestartMessage>, getParentStats, ());
  MOCK_METHOD(void, initialize, (Event::Dispatcher & dispatcher, Server::Instance& server));
  MOCK_METHOD(void, sendParentAdminShutdownRequest, (time_t & original_start_time));
  MOCK_METHOD(void, sendParentTerminateRequest, ());
  MOCK_METHOD(ServerStatsFromParent, mergeParentStatsIfAny, (Stats::StoreRoot & stats_store));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(std::string, version, ());
  MOCK_METHOD(Thread::BasicLockable&, logLock, ());
  MOCK_METHOD(Thread::BasicLockable&, accessLogLock, ());
  MOCK_METHOD(Stats::Allocator&, statsAllocator, ());

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

  DrainManagerPtr
  createDrainManager(envoy::config::listener::v3::Listener::DrainType drain_type) override {
    return DrainManagerPtr{createDrainManager_(drain_type)};
  }
  LdsApiPtr createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config) override {
    return LdsApiPtr{createLdsApi_(lds_config)};
  }

  MOCK_METHOD(LdsApi*, createLdsApi_, (const envoy::config::core::v3::ConfigSource& lds_config));
  MOCK_METHOD(std::vector<Network::FilterFactoryCb>, createNetworkFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
               Configuration::FilterChainFactoryContext& filter_chain_factory_context));
  MOCK_METHOD(std::vector<Network::ListenerFilterFactoryCb>, createListenerFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&,
               Configuration::ListenerFactoryContext& context));
  MOCK_METHOD(std::vector<Network::UdpListenerFilterFactoryCb>, createUdpListenerFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&,
               Configuration::ListenerFactoryContext& context));
  MOCK_METHOD(Network::SocketSharedPtr, createListenSocket,
              (Network::Address::InstanceConstSharedPtr address,
               Network::Address::SocketType socket_type,
               const Network::Socket::OptionsSharedPtr& options,
               const ListenSocketCreationParams& params));
  MOCK_METHOD(DrainManager*, createDrainManager_,
              (envoy::config::listener::v3::Listener::DrainType drain_type));
  MOCK_METHOD(uint64_t, nextListenerTag, ());

  std::shared_ptr<Network::MockListenSocket> socket_;
};

class MockListenerManager : public ListenerManager {
public:
  MockListenerManager();
  ~MockListenerManager() override;

  MOCK_METHOD(bool, addOrUpdateListener,
              (const envoy::config::listener::v3::Listener& config, const std::string& version_info,
               bool modifiable));
  MOCK_METHOD(void, createLdsApi, (const envoy::config::core::v3::ConfigSource& lds_config));
  MOCK_METHOD(std::vector<std::reference_wrapper<Network::ListenerConfig>>, listeners, ());
  MOCK_METHOD(uint64_t, numConnections, (), (const));
  MOCK_METHOD(bool, removeListener, (const std::string& listener_name));
  MOCK_METHOD(void, startWorkers, (GuardDog & guard_dog));
  MOCK_METHOD(void, stopListeners, (StopListenersType listeners_type));
  MOCK_METHOD(void, stopWorkers, ());
  MOCK_METHOD(void, beginListenerUpdate, ());
  MOCK_METHOD(void, endListenerUpdate, (ListenerManager::FailureStates &&));
  MOCK_METHOD(ApiListenerOptRef, apiListener, ());
};

class MockServerLifecycleNotifier : public ServerLifecycleNotifier {
public:
  MockServerLifecycleNotifier();
  ~MockServerLifecycleNotifier() override;

  MOCK_METHOD(ServerLifecycleNotifier::HandlePtr, registerCallback, (Stage, StageCallback));
  MOCK_METHOD(ServerLifecycleNotifier::HandlePtr, registerCallback,
              (Stage, StageCallbackWithCompletion));
};

class MockWorkerFactory : public WorkerFactory {
public:
  MockWorkerFactory();
  ~MockWorkerFactory() override;

  // Server::WorkerFactory
  WorkerPtr createWorker(OverloadManager&, const std::string&) override {
    return WorkerPtr{createWorker_()};
  }

  MOCK_METHOD(Worker*, createWorker_, ());
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
  MOCK_METHOD(void, addListener,
              (Network::ListenerConfig & listener, AddListenerCompletion completion));
  MOCK_METHOD(uint64_t, numConnections, (), (const));
  MOCK_METHOD(void, removeListener,
              (Network::ListenerConfig & listener, std::function<void()> completion));
  MOCK_METHOD(void, start, (GuardDog & guard_dog));
  MOCK_METHOD(void, initializeStats, (Stats::Scope & scope, const std::string& prefix));
  MOCK_METHOD(void, stop, ());
  MOCK_METHOD(void, stopListener,
              (Network::ListenerConfig & listener, std::function<void()> completion));

  AddListenerCompletion add_listener_completion_;
  std::function<void()> remove_listener_completion_;
};

class MockOverloadManager : public OverloadManager {
public:
  MockOverloadManager();
  ~MockOverloadManager() override;

  // OverloadManager
  MOCK_METHOD(void, start, ());
  MOCK_METHOD(bool, registerForAction,
              (const std::string& action, Event::Dispatcher& dispatcher,
               OverloadActionCb callback));
  MOCK_METHOD(ThreadLocalOverloadState&, getThreadLocalOverloadState, ());

  ThreadLocalOverloadState overload_state_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_.get()); }

  MOCK_METHOD(Admin&, admin, ());
  MOCK_METHOD(Api::Api&, api, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::DnsResolverSharedPtr, dnsResolver, ());
  MOCK_METHOD(void, drainListeners, ());
  MOCK_METHOD(DrainManager&, drainManager, ());
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, ());
  MOCK_METHOD(void, failHealthcheck, (bool fail));
  MOCK_METHOD(void, exportStatsToChild, (envoy::HotRestartMessage::Reply::Stats*));
  MOCK_METHOD(bool, healthCheckFailed, ());
  MOCK_METHOD(HotRestart&, hotRestart, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(ListenerManager&, listenerManager, ());
  MOCK_METHOD(Envoy::MutexTracer*, mutexTracer, ());
  MOCK_METHOD(const Options&, options, ());
  MOCK_METHOD(OverloadManager&, overloadManager, ());
  MOCK_METHOD(Runtime::RandomGenerator&, random, ());
  MOCK_METHOD(Runtime::Loader&, runtime, ());
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(bool, isShutdown, ());
  MOCK_METHOD(void, shutdownAdmin, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(time_t, startTimeCurrentEpoch, ());
  MOCK_METHOD(time_t, startTimeFirstEpoch, ());
  MOCK_METHOD(Stats::Store&, stats, ());
  MOCK_METHOD(Grpc::Context&, grpcContext, ());
  MOCK_METHOD(Http::Context&, httpContext, ());
  MOCK_METHOD(ProcessContextOptRef, processContext, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, statsFlushInterval, (), (const));
  MOCK_METHOD(void, flushStats, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(Configuration::ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(Configuration::TransportSocketFactoryContext&, transportSocketFactoryContext, ());

  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) override {
    http_context_.setDefaultTracingConfig(tracing_config);
  }

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
  std::shared_ptr<testing::NiceMock<Configuration::MockTransportSocketFactoryContext>>
      transport_socket_factory_context_;
};

namespace Configuration {

class MockMain : public Main {
public:
  MockMain() : MockMain(0, 0, 0, 0) {}
  MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill);
  ~MockMain() override;

  MOCK_METHOD(Upstream::ClusterManager*, clusterManager, ());
  MOCK_METHOD(std::list<Stats::SinkPtr>&, statsSinks, ());
  MOCK_METHOD(std::chrono::milliseconds, statsFlushInterval, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMissTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMegaMissTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdKillTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMultiKillTimeout, (), (const));

  std::chrono::milliseconds wd_miss_;
  std::chrono::milliseconds wd_megamiss_;
  std::chrono::milliseconds wd_kill_;
  std::chrono::milliseconds wd_multikill_;
};

class MockServerFactoryContext : public virtual ServerFactoryContext {
public:
  MockServerFactoryContext();
  ~MockServerFactoryContext() override;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(Envoy::Runtime::RandomGenerator&, random, ());
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(Server::Admin&, admin, ());
  MOCK_METHOD(TimeSource&, timeSource, ());
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
  Grpc::Context& grpcContext() override { return grpc_context_; }

  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Envoy::Runtime::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> scope_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Api::MockApi> api_;
  Grpc::ContextImpl grpc_context_;
};

class MockFactoryContext : public virtual FactoryContext {
public:
  MockFactoryContext();
  ~MockFactoryContext() override;

  MOCK_METHOD(ServerFactoryContext&, getServerFactoryContext, (), (const));
  MOCK_METHOD(TransportSocketFactoryContext&, getTransportSocketFactoryContext, (), (const));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(bool, healthCheckFailed, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(Envoy::Runtime::RandomGenerator&, random, ());
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(OverloadManager&, overloadManager, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(Server::Admin&, admin, ());
  MOCK_METHOD(Stats::Scope&, listenerScope, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, listenerMetadata, (), (const));
  MOCK_METHOD(envoy::config::core::v3::TrafficDirection, direction, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, ());
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }
  MOCK_METHOD(ProcessContextOptRef, processContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());

  testing::NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
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
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
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

  MOCK_METHOD(Server::Admin&, admin, ());
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Envoy::Runtime::RandomGenerator&, random, ());
  MOCK_METHOD(Stats::Store&, stats, ());
  MOCK_METHOD(Init::Manager*, initManager, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(ThreadLocal::SlotAllocator&, threadLocal, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());

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
  MOCK_METHOD(const Network::ListenerConfig&, listenerConfig_, (), (const));

  Network::MockListenerConfig listener_config_;
};

class MockHealthCheckerFactoryContext : public virtual HealthCheckerFactoryContext {
public:
  MockHealthCheckerFactoryContext();
  ~MockHealthCheckerFactoryContext() override;

  MOCK_METHOD(Upstream::Cluster&, cluster, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Envoy::Runtime::RandomGenerator&, random, ());
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Upstream::HealthCheckEventLogger*, eventLogger_, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
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

class MockFilterChainFactoryContext : public MockFactoryContext, public FilterChainFactoryContext {
public:
  MockFilterChainFactoryContext();
  ~MockFilterChainFactoryContext() override;
};

class MockTracerFactoryContext : public TracerFactoryContext {
public:
  MockTracerFactoryContext();
  ~MockTracerFactoryContext() override;

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());

  testing::NiceMock<Configuration::MockServerFactoryContext> server_factory_context_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
