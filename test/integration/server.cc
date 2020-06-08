#include "test/integration/server.h"

#include <memory>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/thread.h"
#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"
#include "common/stats/symbol_table_creator.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/hot_restart_nop_impl.h"
#include "server/options_impl.h"
#include "server/process_context_impl.h"

#include "test/common/runtime/utility.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

OptionsImpl createTestOptionsImpl(const std::string& config_path, const std::string& config_yaml,
                                  Network::Address::IpVersion ip_version,
                                  FieldValidationConfig validation_config, uint32_t concurrency,
                                  std::chrono::seconds drain_time,
                                  Server::DrainStrategy drain_strategy) {
  OptionsImpl test_options("cluster_name", "node_name", "zone_name", spdlog::level::info);

  test_options.setConfigPath(config_path);
  test_options.setConfigYaml(config_yaml);
  test_options.setLocalAddressIpVersion(ip_version);
  test_options.setFileFlushIntervalMsec(std::chrono::milliseconds(50));
  test_options.setDrainTime(drain_time);
  test_options.setParentShutdownTime(std::chrono::seconds(2));
  test_options.setDrainStrategy(drain_strategy);
  test_options.setAllowUnkownFields(validation_config.allow_unknown_static_fields);
  test_options.setRejectUnknownFieldsDynamic(validation_config.reject_unknown_dynamic_fields);
  test_options.setIgnoreUnknownFieldsDynamic(validation_config.ignore_unknown_dynamic_fields);
  test_options.setConcurrency(concurrency);
  test_options.setHotRestartDisabled(true);

  return test_options;
}

} // namespace Server

IntegrationTestServerPtr IntegrationTestServer::create(
    const std::string& config_path, const Network::Address::IpVersion version,
    std::function<void(IntegrationTestServer&)> server_ready_function,
    std::function<void()> on_server_init_function, bool deterministic,
    Event::TestTimeSystem& time_system, Api::Api& api, bool defer_listener_finalization,
    ProcessObjectOptRef process_object, Server::FieldValidationConfig validation_config,
    uint32_t concurrency, std::chrono::seconds drain_time, Server::DrainStrategy drain_strategy,
    bool use_real_stats) {
  IntegrationTestServerPtr server{
      std::make_unique<IntegrationTestServerImpl>(time_system, api, config_path, use_real_stats)};
  if (server_ready_function != nullptr) {
    server->setOnServerReadyCb(server_ready_function);
  }
  server->start(version, on_server_init_function, deterministic, defer_listener_finalization,
                process_object, validation_config, concurrency, drain_time, drain_strategy);
  return server;
}

void IntegrationTestServer::waitUntilListenersReady() {
  Thread::LockGuard guard(listeners_mutex_);
  while (pending_listeners_ != 0) {
    // If your test is hanging forever here, you may need to create your listener manually,
    // after BaseIntegrationTest::initialize() is done. See cds_integration_test.cc for an example.
    listeners_cv_.wait(listeners_mutex_); // Safe since CondVar::wait won't throw.
  }
  ENVOY_LOG(info, "listener wait complete");
}

void IntegrationTestServer::start(const Network::Address::IpVersion version,
                                  std::function<void()> on_server_init_function, bool deterministic,
                                  bool defer_listener_finalization,
                                  ProcessObjectOptRef process_object,
                                  Server::FieldValidationConfig validator_config,
                                  uint32_t concurrency, std::chrono::seconds drain_time,
                                  Server::DrainStrategy drain_strategy) {
  ENVOY_LOG(info, "starting integration test server");
  ASSERT(!thread_);
  thread_ =
      api_.threadFactory().createThread([version, deterministic, process_object, validator_config,
                                         concurrency, drain_time, drain_strategy, this]() -> void {
        threadRoutine(version, deterministic, process_object, validator_config, concurrency,
                      drain_time, drain_strategy);
      });

  // If any steps need to be done prior to workers starting, do them now. E.g., xDS pre-init.
  // Note that there is no synchronization guaranteeing this happens either
  // before workers starting or after server start. Any needed synchronization must occur in the
  // routines. These steps are executed at this point in the code to allow server initialization to
  // be dependent on them (e.g. control plane peers).
  if (on_server_init_function != nullptr) {
    on_server_init_function();
  }

  // Wait for the server to be created and the number of initial listeners to wait for to be set.
  server_set_.waitReady();

  if (!defer_listener_finalization) {
    // Now wait for the initial listeners (if any) to actually be listening on the worker.
    // At this point the server is up and ready for testing.
    waitUntilListenersReady();
  }

  // If we are tapping, spin up tcpdump.
  const auto tap_path = TestEnvironment::getOptionalEnvVar("TAP_PATH");
  if (tap_path) {
    std::vector<uint32_t> ports;
    for (auto listener : server().listenerManager().listeners()) {
      const auto listen_addr = listener.get().listenSocketFactory().localAddress();
      if (listen_addr->type() == Network::Address::Type::Ip) {
        ports.push_back(listen_addr->ip()->port());
      }
    }
    // TODO(htuch): Support a different loopback interface as needed.
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test_id =
        std::string(test_info->name()) + "_" + std::string(test_info->test_case_name());
    const std::string pcap_path =
        tap_path.value() + "_" + absl::StrReplaceAll(test_id, {{"/", "_"}}) + "_server.pcap";
    tcp_dump_ = std::make_unique<TcpDump>(pcap_path, "lo", ports);
  }
}

IntegrationTestServer::~IntegrationTestServer() {
  // Derived class must have shutdown server.
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

void IntegrationTestServer::serverReady() {
  pending_listeners_ = server().listenerManager().listeners().size();
  if (on_server_ready_cb_ != nullptr) {
    on_server_ready_cb_(*this);
  }
  server_set_.setReady();
}

void IntegrationTestServer::threadRoutine(const Network::Address::IpVersion version,
                                          bool deterministic, ProcessObjectOptRef process_object,
                                          Server::FieldValidationConfig validation_config,
                                          uint32_t concurrency, std::chrono::seconds drain_time,
                                          Server::DrainStrategy drain_strategy) {
  OptionsImpl options(Server::createTestOptionsImpl(config_path_, "", version, validation_config,
                                                    concurrency, drain_time, drain_strategy));
  Thread::MutexBasicLockable lock;

  Runtime::RandomGeneratorPtr random_generator;
  if (deterministic) {
    random_generator = std::make_unique<testing::NiceMock<Runtime::MockRandomGenerator>>();
  } else {
    random_generator = std::make_unique<Runtime::RandomGeneratorImpl>();
  }
  createAndRunEnvoyServer(options, time_system_, Network::Utility::getLocalAddress(version), *this,
                          lock, *this, std::move(random_generator), process_object);
}

IntegrationTestServerImpl::IntegrationTestServerImpl(Event::TestTimeSystem& time_system,
                                                     Api::Api& api, const std::string& config_path,
                                                     bool use_real_stats)
    : IntegrationTestServer(time_system, api, config_path),
      symbol_table_(Stats::SymbolTableCreator::makeSymbolTable()) {
  stats_allocator_ =
      (use_real_stats ? std::make_unique<Stats::AllocatorImpl>(*symbol_table_)
                      : std::make_unique<Stats::NotifyingAllocatorImpl>(*symbol_table_));
}

void IntegrationTestServerImpl::createAndRunEnvoyServer(
    OptionsImpl& options, Event::TimeSystem& time_system,
    Network::Address::InstanceConstSharedPtr local_address, ListenerHooks& hooks,
    Thread::BasicLockable& access_log_lock, Server::ComponentFactory& component_factory,
    Runtime::RandomGeneratorPtr&& random_generator, ProcessObjectOptRef process_object) {
  {
    Init::ManagerImpl init_manager{"Server"};
    Server::HotRestartNopImpl restarter;
    ThreadLocal::InstanceImpl tls;
    Stats::ThreadLocalStoreImpl stat_store(*stats_allocator_);
    std::unique_ptr<ProcessContext> process_context;
    if (process_object.has_value()) {
      process_context = std::make_unique<ProcessContextImpl>(process_object->get());
    }
    Server::InstanceImpl server(init_manager, options, time_system, local_address, hooks, restarter,
                                stat_store, access_log_lock, component_factory,
                                std::move(random_generator), tls, Thread::threadFactoryForTest(),
                                Filesystem::fileSystemForTest(), std::move(process_context));
    // This is technically thread unsafe (assigning to a shared_ptr accessed
    // across threads), but because we synchronize below through serverReady(), the only
    // consumer on the main test thread in ~IntegrationTestServerImpl will not race.
    admin_address_ = server.admin().socket().localAddress();
    server_ = &server;
    stat_store_ = &stat_store;
    serverReady();
    server.run();
  }
  server_gone_.Notify();
}

IntegrationTestServerImpl::~IntegrationTestServerImpl() {
  ENVOY_LOG(info, "stopping integration test server");

  if (useAdminInterfaceToQuit()) {
    Network::Address::InstanceConstSharedPtr admin_address(admin_address_);
    if (admin_address != nullptr) {
      BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
          admin_address, "POST", "/quitquitquit", "", Http::CodecClient::Type::HTTP1);
      EXPECT_TRUE(response->complete());
      EXPECT_EQ("200", response->headers().getStatusValue());
      server_gone_.WaitForNotification();
    }
  } else {
    if (server_) {
      server_->dispatcher().post([this]() { server_->shutdown(); });
      server_gone_.WaitForNotification();
    }
  }

  server_ = nullptr;
  admin_address_ = nullptr;
  stat_store_ = nullptr;
}

} // namespace Envoy
