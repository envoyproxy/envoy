#include "exe/main_common.h"

#include <iostream>
#include <memory>
#include <new>

#include "envoy/config/listener/v3/listener.pb.h"

#include "common/common/compiler_requirements.h"
#include "common/common/perf_annotation.h"
#include "common/network/utility.h"
#include "common/stats/symbol_table_creator.h"
#include "common/stats/thread_local_store.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/hot_restart_nop_impl.h"
#include "server/listener_hooks.h"
#include "server/options_impl.h"
#include "server/server.h"

#include "absl/strings/str_split.h"

#ifdef ENVOY_HOT_RESTART
#include "server/hot_restart_impl.h"
#endif

namespace Envoy {

Server::DrainManagerPtr ProdComponentFactory::createDrainManager(Server::Instance& server) {
  // The global drain manager only triggers on listener modification, which effectively is
  // hot restart at the global level. The per-listener drain managers decide whether to
  // to include /healthcheck/fail status.
  return std::make_unique<Server::DrainManagerImpl>(
      server, envoy::config::listener::v3::Listener::MODIFY_ONLY);
}

Runtime::LoaderPtr ProdComponentFactory::createRuntime(Server::Instance& server,
                                                       Server::Configuration::Initial& config) {
  return Server::InstanceUtil::createRuntime(server, config);
}

MainCommonBase::MainCommonBase(const OptionsImpl& options, Event::TimeSystem& time_system,
                               ListenerHooks& listener_hooks,
                               Server::ComponentFactory& component_factory,
                               std::unique_ptr<Runtime::RandomGenerator>&& random_generator,
                               Thread::ThreadFactory& thread_factory,
                               Filesystem::Instance& file_system,
                               std::unique_ptr<ProcessContext> process_context)
    : options_(options), component_factory_(component_factory), thread_factory_(thread_factory),
      file_system_(file_system), symbol_table_(Stats::SymbolTableCreator::initAndMakeSymbolTable(
                                     options_.fakeSymbolTableEnabled())),
      stats_allocator_(*symbol_table_) {
  // Process the option to disable extensions as early as possible,
  // before we do any configuration loading.
  OptionsImpl::disableExtensions(options.disabledExtensions());

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
    logging_context_ = std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(),
                                                         log_lock, options_.logFormatEscaped());

    configureComponentLogLevels();

    // Provide consistent behavior for out-of-memory, regardless of whether it occurs in a try/catch
    // block or not.
    std::set_new_handler([]() { PANIC("out of memory"); });

    stats_store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(stats_allocator_);

    server_ = std::make_unique<Server::InstanceImpl>(
        *init_manager_, options_, time_system, local_address, listener_hooks, *restarter_,
        *stats_store_, access_log_lock, component_factory, std::move(random_generator), *tls_,
        thread_factory_, file_system_, std::move(process_context));

    break;
  }
  case Server::Mode::Validate:
    restarter_ = std::make_unique<Server::HotRestartNopImpl>();
    logging_context_ =
        std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(),
                                          restarter_->logLock(), options_.logFormatEscaped());
    break;
  }
}

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
    return Server::validateConfig(options_, local_address, component_factory_, thread_factory_,
                                  file_system_);
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
    Http::ResponseHeaderMapImpl response_headers;
    std::string body;
    server_->admin().request(path_and_query_buf, method_buf, response_headers, body);
    handler(response_headers, body);
  });
}

MainCommon::MainCommon(int argc, const char* const* argv)
    : options_(argc, argv, &MainCommon::hotRestartVersion, spdlog::level::info),
      base_(options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
            std::make_unique<Runtime::RandomGeneratorImpl>(), platform_impl_.threadFactory(),
            platform_impl_.fileSystem(), nullptr) {}

std::string MainCommon::hotRestartVersion(bool hot_restart_enabled) {
#ifdef ENVOY_HOT_RESTART
  if (hot_restart_enabled) {
    return Server::HotRestartImpl::hotRestartVersion();
  }
#else
  UNREFERENCED_PARAMETER(hot_restart_enabled);
#endif
  return "disabled";
}

} // namespace Envoy
