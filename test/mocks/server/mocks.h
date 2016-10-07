#pragma once

#include "envoy/server/admin.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/ssl/context_manager.h"

#include "common/common/thread.h"
#include "common/ssl/context_manager_impl.h"
#include "common/stats/stats_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::NiceMock;

namespace Server {

class MockOptions : public Options {
public:
  MockOptions();
  ~MockOptions();

  MOCK_METHOD0(baseId, uint64_t());
  MOCK_METHOD0(concurrency, uint32_t());
  MOCK_METHOD0(configPath, const std::string&());
  MOCK_METHOD0(logLevel, spdlog::level::level_enum());
  MOCK_METHOD0(restartEpoch, uint64_t());
  MOCK_METHOD0(serviceClusterName, const std::string&());
  MOCK_METHOD0(serviceNodeName, const std::string&());
  MOCK_METHOD0(serviceZone, const std::string&());
  MOCK_METHOD0(fileFlushIntervalMsec, std::chrono::milliseconds());
};

class MockAdmin : public Admin {
public:
  MockAdmin();
  ~MockAdmin();

  // Server::Admin
  MOCK_METHOD3(addHandler,
               void(const std::string& prefix, const std::string& help_text, HandlerCb callback));
};

class MockDrainManager : public DrainManager {
public:
  MockDrainManager();
  ~MockDrainManager();

  // Server::DrainManager
  MOCK_METHOD0(drainClose, bool());
  MOCK_METHOD0(draining, bool());
  MOCK_METHOD0(startDrainSequence, void());
  MOCK_METHOD0(startParentShutdownSequence, void());
};

class MockHotRestart : public HotRestart {
public:
  MockHotRestart();
  ~MockHotRestart();

  // Server::HotRestart
  MOCK_METHOD0(drainParentListeners, void());
  MOCK_METHOD1(duplicateParentListenSocket, int(uint32_t port));
  MOCK_METHOD1(getParentStats, void(GetParentStatsInfo& info));
  MOCK_METHOD2(initialize, void(Event::Dispatcher& dispatcher, Server::Instance& server));
  MOCK_METHOD1(shutdownParentAdmin, void(ShutdownParentAdminInfo& info));
  MOCK_METHOD0(terminateParent, void());
  MOCK_METHOD0(version, std::string());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Server::Instance
  RateLimit::ClientPtr rateLimitClient(const Optional<std::chrono::milliseconds>&) override {
    return RateLimit::ClientPtr{rateLimitClient_()};
  }

  MOCK_METHOD0(accessLogLock, Thread::BasicLockable&());
  MOCK_METHOD0(admin, Admin&());
  MOCK_METHOD0(api, Api::Api&());
  MOCK_METHOD0(clusterManager, Upstream::ClusterManager&());
  MOCK_METHOD0(sslContextManager, Ssl::ContextManager&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(dnsResolver, Network::DnsResolver&());
  MOCK_METHOD0(draining, bool());
  MOCK_METHOD0(drainListeners, void());
  MOCK_METHOD0(drainManager, DrainManager&());
  MOCK_METHOD0(accessLogManager, AccessLog::AccessLogManager&());
  MOCK_METHOD1(failHealthcheck, void(bool fail));
  MOCK_METHOD1(getListenSocketFd, int(uint32_t port));
  MOCK_METHOD1(getParentStats, void(HotRestart::GetParentStatsInfo&));
  MOCK_METHOD0(healthCheckFailed, bool());
  MOCK_METHOD0(hotRestart, HotRestart&());
  MOCK_METHOD0(options, Options&());
  MOCK_METHOD0(random, Runtime::RandomGenerator&());
  MOCK_METHOD0(rateLimitClient_, RateLimit::Client*());
  MOCK_METHOD0(runtime, Runtime::Loader&());
  MOCK_METHOD0(shutdown, void());
  MOCK_METHOD0(shutdownAdmin, void());
  MOCK_METHOD0(startTimeCurrentEpoch, time_t());
  MOCK_METHOD0(startTimeFirstEpoch, time_t());
  MOCK_METHOD0(stats, Stats::Store&());
  MOCK_METHOD0(httpTracer, Tracing::HttpTracer&());
  MOCK_METHOD0(threadLocal, ThreadLocal::Instance&());
  MOCK_METHOD0(getLocalAddress, const std::string&());

  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  testing::NiceMock<Tracing::MockHttpTracer> http_tracer_;
  testing::NiceMock<Network::MockDnsResolver> dns_resolver_;
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
};

} // Server
