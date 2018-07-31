#include "test/integration/server.h"

#include <string>

#include "envoy/http/header_map.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/hot_restart_nop_impl.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {

IntegrationTestServerPtr IntegrationTestServer::create(
    const std::string& config_path, const Network::Address::IpVersion version,
    std::function<void()> pre_worker_start_test_steps, bool deterministic) {
  IntegrationTestServerPtr server{new IntegrationTestServer(config_path)};
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

  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(server_->admin().socket().localAddress(), "POST",
                                         "/quitquitquit", "", Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

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
  Server::TestOptionsImpl options(config_path_, version);
  Server::HotRestartNopImpl restarter;
  Thread::MutexBasicLockable lock;

  ThreadLocal::InstanceImpl tls;
  Stats::HeapRawStatDataAllocator stats_allocator;
  Stats::StatsOptionsImpl options_;
  Stats::ThreadLocalStoreImpl stats_store(options_, stats_allocator);
  stat_store_ = &stats_store;
  Runtime::RandomGeneratorPtr random_generator;
  if (deterministic) {
    random_generator = std::make_unique<testing::NiceMock<Runtime::MockRandomGenerator>>();
  } else {
    random_generator = std::make_unique<Runtime::RandomGeneratorImpl>();
  }
  server_.reset(new Server::InstanceImpl(options, Network::Utility::getLocalAddress(version), *this,
                                         restarter, stats_store, lock, *this,
                                         std::move(random_generator), tls));
  pending_listeners_ = server_->listenerManager().listeners().size();
  ENVOY_LOG(info, "waiting for {} test server listeners", pending_listeners_);
  server_set_.setReady();
  server_->run();
  server_.reset();
  stat_store_ = nullptr;
}

Server::TestOptionsImpl Server::TestOptionsImpl::asConfigYaml() {
  return TestOptionsImpl("", Filesystem::fileReadToEnd(config_path_), local_address_ip_version_);
}

} // namespace Envoy
