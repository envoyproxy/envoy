#include "exe/main_common.h"

#include <iostream>
#include <memory>

#include "common/common/compiler_requirements.h"
#include "common/common/perf_annotation.h"
#include "common/event/libevent.h"
#include "common/network/utility.h"
#include "common/stats/thread_local_store.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/hot_restart_nop_impl.h"
#include "server/options_impl.h"
#include "server/proto_descriptors.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "absl/strings/str_split.h"

#ifdef ENVOY_HOT_RESTART
#include "server/hot_restart_impl.h"
#endif

#include "ares.h"

namespace Envoy {

Server::DrainManagerPtr ProdComponentFactory::createDrainManager(Server::Instance& server) {
  // The global drain manager only triggers on listener modification, which effectively is
  // hot restart at the global level. The per-listener drain managers decide whether to
  // to include /healthcheck/fail status.
  return std::make_unique<Server::DrainManagerImpl>(server,
                                                    envoy::api::v2::Listener_DrainType_MODIFY_ONLY);
}

Runtime::LoaderPtr ProdComponentFactory::createRuntime(Server::Instance& server,
                                                       Server::Configuration::Initial& config) {
  return Server::InstanceUtil::createRuntime(server, config);
}

MainCommonBase::MainCommonBase(OptionsImpl& options, Event::TimeSystem& time_system,
                               TestHooks& test_hooks, Server::ComponentFactory& component_factory,
                               std::unique_ptr<Runtime::RandomGenerator>&& random_generator)
    : options_(options), component_factory_(component_factory) {
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  RELEASE_ASSERT(Envoy::Server::validateProtoDescriptors(), "");

  switch (options_.mode()) {
  case Server::Mode::InitOnly:
  case Server::Mode::Serve: {
#ifdef ENVOY_HOT_RESTART
    if (!options.hotRestartDisabled()) {
      restarter_ = std::make_unique<Server::HotRestartImpl>(options_);
    }
#endif
    if (restarter_ == nullptr) {
      restarter_ = std::make_unique<Server::HotRestartNopImpl>();
    }

    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    Thread::BasicLockable& log_lock = restarter_->logLock();
    Thread::BasicLockable& access_log_lock = restarter_->accessLogLock();
    auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
    logging_context_ =
        std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(), log_lock);

    configureComponentLogLevels();

    stats_store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(options_.statsOptions(),
                                                                 restarter_->statsAllocator());

    server_ = std::make_unique<Server::InstanceImpl>(
        options_, time_system, local_address, test_hooks, *restarter_, *stats_store_,
        access_log_lock, component_factory, std::move(random_generator), *tls_);
    break;
  }
  case Server::Mode::Validate:
    restarter_ = std::make_unique<Server::HotRestartNopImpl>();
    logging_context_ = std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(),
                                                         restarter_->logLock());
    break;
  }
}

MainCommonBase::~MainCommonBase() { ares_library_cleanup(); }

void MainCommonBase::configureComponentLogLevels() {
  for (auto& component_log_level : options_.componentLogLevels()) {
    Logger::Logger* logger_to_change = Logger::Registry::logger(component_log_level.first);
    ASSERT(logger_to_change);
    logger_to_change->setLevel(component_log_level.second);
  }
}

bool MainCommonBase::run() {
  switch (options_.mode()) {
  case Server::Mode::Serve:
    server_->run();
    return true;
  case Server::Mode::Validate: {
    auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
    return Server::validateConfig(options_, local_address, component_factory_);
  }
  case Server::Mode::InitOnly:
    PERF_DUMP();
    return true;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void MainCommonBase::adminRequest(absl::string_view path_and_query, absl::string_view method,
                                  const AdminRequestFn& handler) {
  std::string path_and_query_buf = std::string(path_and_query);
  std::string method_buf = std::string(method);
  server_->dispatcher().post([this, path_and_query_buf, method_buf, handler]() {
    Http::HeaderMapImpl response_headers;
    std::string body;
    server_->admin().request(path_and_query_buf, method_buf, response_headers, body);
    handler(response_headers, body);
  });
}

MainCommon::MainCommon(int argc, const char* const* argv)
    : options_(argc, argv, &MainCommon::hotRestartVersion, spdlog::level::info),
      base_(options_, real_time_system_, default_test_hooks_, prod_component_factory_,
            std::make_unique<Runtime::RandomGeneratorImpl>()) {}

std::string MainCommon::hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len,
                                          bool hot_restart_enabled) {
#ifdef ENVOY_HOT_RESTART
  if (hot_restart_enabled) {
    return Server::HotRestartImpl::hotRestartVersion(max_num_stats, max_stat_name_len);
  }
#else
  UNREFERENCED_PARAMETER(hot_restart_enabled);
  UNREFERENCED_PARAMETER(max_num_stats);
  UNREFERENCED_PARAMETER(max_stat_name_len);
#endif
  return "disabled";
}

// Legacy implementation of main_common.
//
// TODO(jmarantz): Remove this when all callers are removed. At that time, MainCommonBase
// and MainCommon can be merged. The current theory is that only Google calls this.
int main_common(OptionsImpl& options) {
  try {
    Event::RealTimeSystem real_time_system_;
    DefaultTestHooks default_test_hooks_;
    ProdComponentFactory prod_component_factory_;
    MainCommonBase main_common(options, real_time_system_, default_test_hooks_,
                               prod_component_factory_,
                               std::make_unique<Runtime::RandomGeneratorImpl>());
    return main_common.run() ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (EnvoyException& e) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

} // namespace Envoy
