#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

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
#include "envoy/stats/stats_options.h"
#include "envoy/thread/thread.h"

#include "common/secret/secret_manager_impl.h"
#include "common/ssl/context_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

class MockOptions : public Options {
public:
  MockOptions() : MockOptions(std::string()) {}
  MockOptions(const std::string& config_path);
  ~MockOptions();

  MOCK_CONST_METHOD0(baseId, uint64_t());
  MOCK_CONST_METHOD0(concurrency, uint32_t());
  MOCK_CONST_METHOD0(configPath, const std::string&());
  MOCK_CONST_METHOD0(configYaml, const std::string&());
  MOCK_CONST_METHOD0(v2ConfigOnly, bool());
  MOCK_CONST_METHOD0(adminAddressPath, const std::string&());
  MOCK_CONST_METHOD0(localAddressIpVersion, Network::Address::IpVersion());
  MOCK_CONST_METHOD0(drainTime, std::chrono::seconds());
  MOCK_CONST_METHOD0(logLevel, spdlog::level::level_enum());
  MOCK_CONST_METHOD0(logFormat, const std::string&());
  MOCK_CONST_METHOD0(logPath, const std::string&());
  MOCK_CONST_METHOD0(parentShutdownTime, std::chrono::seconds());
  MOCK_CONST_METHOD0(restartEpoch, uint64_t());
  MOCK_CONST_METHOD0(fileFlushIntervalMsec, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(mode, Mode());
  MOCK_CONST_METHOD0(serviceClusterName, const std::string&());
  MOCK_CONST_METHOD0(serviceNodeName, const std::string&());
  MOCK_CONST_METHOD0(serviceZone, const std::string&());
  MOCK_CONST_METHOD0(maxStats, uint64_t());
  MOCK_CONST_METHOD0(statsOptions, const Stats::StatsOptions&());
  MOCK_CONST_METHOD0(hotRestartDisabled, bool());

  std::string config_path_;
  std::string config_yaml_;
  bool v2_config_only_{};
  std::string admin_address_path_;
  std::string service_cluster_name_;
  std::string service_node_name_;
  std::string service_zone_name_;
  std::string log_path_;
  Stats::StatsOptionsImpl stats_options_;
  bool hot_restart_disabled_{};
};

class MockConfigTracker : public ConfigTracker {
public:
  MockConfigTracker();
  ~MockConfigTracker();

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
  ~MockAdmin();

  // Server::Admin
  MOCK_METHOD5(addHandler, bool(const std::string& prefix, const std::string& help_text,
                                HandlerCb callback, bool removable, bool mutates_server_state));
  MOCK_METHOD1(removeHandler, bool(const std::string& prefix));
  MOCK_METHOD0(socket, Network::Socket&());
  MOCK_METHOD0(getConfigTracker, ConfigTracker&());
  MOCK_METHOD5(request,
               Http::Code(absl::string_view path, const Http::Utility::QueryParams& query_params,
                          absl::string_view method, Http::HeaderMap& response_headers,
                          std::string& body));

  NiceMock<MockConfigTracker> config_tracker_;
};

class MockDrainManager : public DrainManager {
public:
  MockDrainManager();
  ~MockDrainManager();

  // Server::DrainManager
  MOCK_CONST_METHOD0(drainClose, bool());
  MOCK_METHOD1(startDrainSequence, void(std::function<void()> completion));
  MOCK_METHOD0(startParentShutdownSequence, void());

  std::function<void()> drain_sequence_completion_;
};

class MockWatchDog : public WatchDog {
public:
  MockWatchDog();
  ~MockWatchDog();

  // Server::WatchDog
  MOCK_METHOD1(startWatchdog, void(Event::Dispatcher& dispatcher));
  MOCK_METHOD0(touch, void());
  MOCK_CONST_METHOD0(threadId, int32_t());
  MOCK_CONST_METHOD0(lastTouchTime, MonotonicTime());
};

class MockGuardDog : public GuardDog {
public:
  MockGuardDog();
  ~MockGuardDog();

  // Server::GuardDog
  MOCK_METHOD1(createWatchDog, WatchDogSharedPtr(int32_t thread_id));
  MOCK_METHOD1(stopWatching, void(WatchDogSharedPtr wd));

  std::shared_ptr<MockWatchDog> watch_dog_;
};

class MockHotRestart : public HotRestart {
public:
  MockHotRestart();
  ~MockHotRestart();

  // Server::HotRestart
  MOCK_METHOD0(drainParentListeners, void());
  MOCK_METHOD1(duplicateParentListenSocket, int(const std::string& address));
  MOCK_METHOD1(getParentStats, void(GetParentStatsInfo& info));
  MOCK_METHOD2(initialize, void(Event::Dispatcher& dispatcher, Server::Instance& server));
  MOCK_METHOD1(shutdownParentAdmin, void(ShutdownParentAdminInfo& info));
  MOCK_METHOD0(terminateParent, void());
  MOCK_METHOD0(shutdown, void());
  MOCK_METHOD0(version, std::string());
  MOCK_METHOD0(logLock, Thread::BasicLockable&());
  MOCK_METHOD0(accessLogLock, Thread::BasicLockable&());
  MOCK_METHOD0(statsAllocator, Stats::StatDataAllocator&());

private:
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable access_log_lock_;
  Stats::HeapStatDataAllocator stats_allocator_;
};

class MockListenerComponentFactory : public ListenerComponentFactory {
public:
  MockListenerComponentFactory();
  ~MockListenerComponentFactory();

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
  MOCK_METHOD3(createListenSocket,
               Network::SocketSharedPtr(Network::Address::InstanceConstSharedPtr address,
                                        const Network::Socket::OptionsSharedPtr& options,
                                        bool bind_to_port));
  MOCK_METHOD1(createDrainManager_, DrainManager*(envoy::api::v2::Listener::DrainType drain_type));
  MOCK_METHOD0(nextListenerTag, uint64_t());

  std::shared_ptr<Network::MockListenSocket> socket_;
};

class MockListenerManager : public ListenerManager {
public:
  MockListenerManager();
  ~MockListenerManager();

  MOCK_METHOD3(addOrUpdateListener, bool(const envoy::api::v2::Listener& config,
                                         const std::string& version_info, bool modifiable));
  MOCK_METHOD1(createLdsApi, void(const envoy::api::v2::core::ConfigSource& lds_config));
  MOCK_METHOD0(listeners, std::vector<std::reference_wrapper<Network::ListenerConfig>>());
  MOCK_METHOD0(numConnections, uint64_t());
  MOCK_METHOD1(removeListener, bool(const std::string& listener_name));
  MOCK_METHOD1(startWorkers, void(GuardDog& guard_dog));
  MOCK_METHOD0(stopListeners, void());
  MOCK_METHOD0(stopWorkers, void());
};

class MockWorkerFactory : public WorkerFactory {
public:
  MockWorkerFactory();
  ~MockWorkerFactory();

  // Server::WorkerFactory
  WorkerPtr createWorker() override { return WorkerPtr{createWorker_()}; }

  MOCK_METHOD0(createWorker_, Worker*());
};

class MockWorker : public Worker {
public:
  MockWorker();
  ~MockWorker();

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
  MOCK_METHOD0(stop, void());
  MOCK_METHOD1(stopListener, void(Network::ListenerConfig& listener));
  MOCK_METHOD0(stopListeners, void());

  AddListenerCompletion add_listener_completion_;
  std::function<void()> remove_listener_completion_;
};

class MockOverloadManager : public OverloadManager {
public:
  MockOverloadManager() {}
  ~MockOverloadManager() {}

  // OverloadManager
  MOCK_METHOD0(start, void());
  MOCK_METHOD3(registerForAction, void(const std::string& action, Event::Dispatcher& dispatcher,
                                       OverloadActionCb callback));
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Server::Instance
  RateLimit::ClientPtr rateLimitClient(const absl::optional<std::chrono::milliseconds>&) override {
    return RateLimit::ClientPtr{rateLimitClient_()};
  }

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
  MOCK_METHOD1(getParentStats, void(HotRestart::GetParentStatsInfo&));
  MOCK_METHOD0(healthCheckFailed, bool());
  MOCK_METHOD0(hotRestart, HotRestart&());
  MOCK_METHOD0(initManager, Init::Manager&());
  MOCK_METHOD0(listenerManager, ListenerManager&());
  MOCK_METHOD0(options, Options&());
  MOCK_METHOD0(overloadManager, OverloadManager&());
  MOCK_METHOD0(random, Runtime::RandomGenerator&());
  MOCK_METHOD0(rateLimitClient_, RateLimit::Client*());
  MOCK_METHOD0(runtime, Runtime::Loader&());
  MOCK_METHOD0(shutdown, void());
  MOCK_METHOD0(shutdownAdmin, void());
  MOCK_METHOD0(singletonManager, Singleton::Manager&());
  MOCK_METHOD0(startTimeCurrentEpoch, time_t());
  MOCK_METHOD0(startTimeFirstEpoch, time_t());
  MOCK_METHOD0(stats, Stats::Store&());
  MOCK_METHOD0(httpTracer, Tracing::HttpTracer&());
  MOCK_METHOD0(threadLocal, ThreadLocal::Instance&());
  MOCK_METHOD0(localInfo, const LocalInfo::LocalInfo&());
  MOCK_CONST_METHOD0(statsFlushInterval, std::chrono::milliseconds());

  std::unique_ptr<Secret::SecretManager> secret_manager_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  testing::NiceMock<Tracing::MockHttpTracer> http_tracer_;
  std::shared_ptr<testing::NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new testing::NiceMock<Network::MockDnsResolver>()};
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockAdmin> admin_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Thread::MutexBasicLockable access_log_lock_;
  testing::NiceMock<Runtime::MockLoader> runtime_loader_;
  Ssl::ContextManagerImpl ssl_context_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<MockHotRestart> hot_restart_;
  testing::NiceMock<MockOptions> options_;
  testing::NiceMock<Runtime::MockRandomGenerator> random_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockListenerManager> listener_manager_;
  testing::NiceMock<MockOverloadManager> overload_manager_;
  Singleton::ManagerPtr singleton_manager_;
};

namespace Configuration {

class MockMain : public Main {
public:
  MockMain() : MockMain(0, 0, 0, 0) {}
  MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill);

  MOCK_METHOD0(clusterManager, Upstream::ClusterManager*());
  MOCK_METHOD0(httpTracer, Tracing::HttpTracer&());
  MOCK_METHOD0(rateLimitClientFactory, RateLimit::ClientFactory&());
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

class MockFactoryContext : public FactoryContext {
public:
  MockFactoryContext();
  ~MockFactoryContext();

  RateLimit::ClientPtr rateLimitClient(const absl::optional<std::chrono::milliseconds>&) override {
    return RateLimit::ClientPtr{rateLimitClient_()};
  }

  MOCK_METHOD0(accessLogManager, AccessLog::AccessLogManager&());
  MOCK_METHOD0(clusterManager, Upstream::ClusterManager&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(drainDecision, const Network::DrainDecision&());
  MOCK_METHOD0(healthCheckFailed, bool());
  MOCK_METHOD0(httpTracer, Tracing::HttpTracer&());
  MOCK_METHOD0(initManager, Init::Manager&());
  MOCK_METHOD0(random, Envoy::Runtime::RandomGenerator&());
  MOCK_METHOD0(rateLimitClient_, RateLimit::Client*());
  MOCK_METHOD0(runtime, Envoy::Runtime::Loader&());
  MOCK_METHOD0(scope, Stats::Scope&());
  MOCK_METHOD0(singletonManager, Singleton::Manager&());
  MOCK_METHOD0(threadLocal, ThreadLocal::Instance&());
  MOCK_METHOD0(admin, Server::Admin&());
  MOCK_METHOD0(listenerScope, Stats::Scope&());
  MOCK_CONST_METHOD0(localInfo, const LocalInfo::LocalInfo&());
  MOCK_CONST_METHOD0(listenerMetadata, const envoy::api::v2::core::Metadata&());
  MOCK_METHOD0(systemTimeSource, SystemTimeSource&());

  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<Tracing::MockHttpTracer> http_tracer_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Envoy::Runtime::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
  Stats::IsolatedStoreImpl scope_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Stats::IsolatedStoreImpl listener_scope_;
  testing::NiceMock<MockSystemTimeSource> system_time_source_;
};

class MockTransportSocketFactoryContext : public TransportSocketFactoryContext {
public:
  MockTransportSocketFactoryContext();
  ~MockTransportSocketFactoryContext();

  MOCK_METHOD0(sslContextManager, Ssl::ContextManager&());
  MOCK_CONST_METHOD0(statsScope, Stats::Scope&());
  MOCK_METHOD0(clusterManager, Upstream::ClusterManager&());
  MOCK_METHOD0(secretManager, Secret::SecretManager&());
  MOCK_METHOD0(local_info, const LocalInfo::LocalInfo&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(random, Envoy::Runtime::RandomGenerator&());
  MOCK_METHOD0(stats, Stats::Store&());
};

class MockListenerFactoryContext : public virtual MockFactoryContext,
                                   public virtual ListenerFactoryContext {
public:
  MockListenerFactoryContext();
  ~MockListenerFactoryContext();

  void addListenSocketOption(const Network::Socket::OptionConstSharedPtr& option) override {
    addListenSocketOption_(option);
  }
  MOCK_METHOD1(addListenSocketOption_, void(const Network::Socket::OptionConstSharedPtr&));
  void addListenSocketOptions(const Network::Socket::OptionsSharedPtr& options) override {
    addListenSocketOptions_(options);
  }
  MOCK_METHOD1(addListenSocketOptions_, void(const Network::Socket::OptionsSharedPtr&));
};

class MockHealthCheckerFactoryContext : public virtual HealthCheckerFactoryContext {
public:
  MockHealthCheckerFactoryContext();
  ~MockHealthCheckerFactoryContext();

  MOCK_METHOD0(cluster, Upstream::Cluster&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(random, Envoy::Runtime::RandomGenerator&());
  MOCK_METHOD0(runtime, Envoy::Runtime::Loader&());
  MOCK_METHOD0(eventLogger_, Upstream::HealthCheckEventLogger*());
  Upstream::HealthCheckEventLoggerPtr eventLogger() override {
    return Upstream::HealthCheckEventLoggerPtr(eventLogger_());
  }

  testing::NiceMock<Upstream::MockCluster> cluster_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<Envoy::Runtime::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_;
  testing::NiceMock<Envoy::Upstream::MockHealthCheckEventLogger>* event_logger_{};
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
