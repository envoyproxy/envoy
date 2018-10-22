#include "test/integration/server.h"

#include <string>

#include "envoy/http/header_map.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/hot_restart_nop_impl.h"
#include "server/options_impl.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

OptionsImpl createTestOptionsImpl(const std::string& config_path, const std::string& config_yaml,
                                  Network::Address::IpVersion ip_version) {
  OptionsImpl test_options("cluster_name", "node_name", "zone_name", spdlog::level::info);

  test_options.setConfigPath(config_path);
  test_options.setConfigYaml(config_yaml);
  test_options.setV2ConfigOnly(false);
  test_options.setLocalAddressIpVersion(ip_version);
  test_options.setFileFlushIntervalMsec(std::chrono::milliseconds(50));
  test_options.setDrainTime(std::chrono::seconds(1));
  test_options.setParentShutdownTime(std::chrono::seconds(2));
  test_options.setMaxStats(16384u);

  return test_options;
}

} // namespace Server

IntegrationTestServerPtr
IntegrationTestServer::create(const std::string& config_path,
                              const Network::Address::IpVersion version,
                              std::function<void()> pre_worker_start_test_steps, bool deterministic,
                              Event::TestTimeSystem& time_system) {
  IntegrationTestServerPtr server{new IntegrationTestServer(time_system, config_path)};
  server->start(version, pre_worker_start_test_steps, deterministic);
  return server;
}

void IntegrationTestServer::start(const Network::Address::IpVersion version,
                                  std::function<void()> pre_worker_start_test_steps,
                                  bool deterministic) {
  ENVOY_LOG(info, "starting integration test server");
  ASSERT(!thread_);
  thread_.reset(new Thread::Thread(
      [version, deterministic, this]() -> void { threadRoutine(version, deterministic); }));

  // If any steps need to be done prior to workers starting, do them now. E.g., xDS pre-init.
  if (pre_worker_start_test_steps != nullptr) {
    pre_worker_start_test_steps();
  }

  // Wait for the server to be created and the number of initial listeners to wait for to be set.
  server_set_.waitReady();

  // Now wait for the initial listeners to actually be listening on the worker. At this point
  // the server is up and ready for testing.
  Thread::LockGuard guard(listeners_mutex_);
  while (pending_listeners_ != 0) {
    listeners_cv_.wait(listeners_mutex_); // Safe since CondVar::wait won't throw.
  }
  ENVOY_LOG(info, "listener wait complete");
}

IntegrationTestServer::~IntegrationTestServer() {
  ENVOY_LOG(info, "stopping integration test server");

  if (admin_address_ != nullptr) {
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        admin_address_, "POST", "/quitquitquit", "", Http::CodecClient::Type::HTTP1);
    EXPECT_TRUE(response->complete());
    EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  }
  thread_->join();
}

void IntegrationTestServer::onWorkerListenerAdded() {
  if (on_worker_listener_added_cb_) {
    on_worker_listener_added_cb_();
  }

  Thread::LockGuard guard(listeners_mutex_);
  if (pending_listeners_ > 0) {
    pending_listeners_--;
    listeners_cv_.notifyOne();
  }
}

void IntegrationTestServer::onWorkerListenerRemoved() {
  if (on_worker_listener_removed_cb_) {
    on_worker_listener_removed_cb_();
  }
}

void IntegrationTestServer::threadRoutine(const Network::Address::IpVersion version,
                                          bool deterministic) {
  OptionsImpl options(Server::createTestOptionsImpl(config_path_, "", version));
  Server::HotRestartNopImpl restarter;
  Thread::MutexBasicLockable lock;

  ThreadLocal::InstanceImpl tls;
  Stats::HeapStatDataAllocator stats_allocator;
  Stats::StatsOptionsImpl stats_options;
  Stats::ThreadLocalStoreImpl stats_store(stats_options, stats_allocator);
  stat_store_ = &stats_store;
  Runtime::RandomGeneratorPtr random_generator;
  if (deterministic) {
    random_generator = std::make_unique<testing::NiceMock<Runtime::MockRandomGenerator>>();
  } else {
    random_generator = std::make_unique<Runtime::RandomGeneratorImpl>();
  }
  server_.reset(new Server::InstanceImpl(
      options, time_system_, Network::Utility::getLocalAddress(version), *this, restarter,
      stats_store, lock, *this, std::move(random_generator), tls));
  pending_listeners_ = server_->listenerManager().listeners().size();
  ENVOY_LOG(info, "waiting for {} test server listeners", pending_listeners_);
  // This is technically thread unsafe (assigning to a shared_ptr accessed
  // across threads), but because we synchronize below on server_set, the only
  // consumer on the main test thread in ~IntegrationTestServer will not race.
  admin_address_ = server_->admin().socket().localAddress();
  server_set_.setReady();
  server_->run();
  server_.reset();
  stat_store_ = nullptr;
}

} // namespace Envoy
