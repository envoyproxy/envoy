#include "exe/main_common.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <new>

#include "envoy/config/listener/v3/listener.pb.h"

#include "common/common/compiler_requirements.h"
#include "common/common/logger.h"
#include "common/common/perf_annotation.h"
#include "common/network/utility.h"
#include "common/stats/thread_local_store.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/hot_restart_nop_impl.h"
#include "server/listener_hooks.h"
#include "server/options_impl.h"
#include "server/server.h"

#include "absl/debugging/symbolize.h"
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
                               std::unique_ptr<Random::RandomGenerator>&& random_generator,
                               Thread::ThreadFactory& thread_factory,
                               Filesystem::Instance& file_system,
                               std::unique_ptr<ProcessContext> process_context)
    : options_(options), component_factory_(component_factory), thread_factory_(thread_factory),
      file_system_(file_system), stats_allocator_(symbol_table_) {
  // Process the option to disable extensions as early as possible,
  // before we do any configuration loading.
  OptionsImpl::disableExtensions(options.disabledExtensions());

  switch (options_.mode()) {
  case Server::Mode::InitOnly:
  case Server::Mode::Serve: {
    configureHotRestarter(*random_generator);

    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    Thread::BasicLockable& log_lock = restarter_->logLock();
    Thread::BasicLockable& access_log_lock = restarter_->accessLogLock();
    auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
    logging_context_ = std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(),
                                                         log_lock, options_.logFormatEscaped(),
                                                         options_.enableFineGrainLogging());

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

void MainCommonBase::configureHotRestarter(Random::RandomGenerator& random_generator) {
#ifdef ENVOY_HOT_RESTART
  if (!options_.hotRestartDisabled()) {
    uint32_t base_id = options_.baseId();

    if (options_.useDynamicBaseId()) {
      ASSERT(options_.restartEpoch() == 0, "cannot use dynamic base id during hot restart");

      std::unique_ptr<Server::HotRestart> restarter;

      // Try 100 times to get an unused base ID and then give up under the assumption
      // that some other problem has occurred to prevent binding the domain socket.
      for (int i = 0; i < 100 && restarter == nullptr; i++) {
        // HotRestartImpl is going to multiply this value by 10, so leave head room.
        base_id = static_cast<uint32_t>(random_generator.random()) & 0x0FFFFFFF;

        try {
          restarter = std::make_unique<Server::HotRestartImpl>(base_id, 0, options_.socketPath(),
                                                               options_.socketMode());
        } catch (Server::HotRestartDomainSocketInUseException& ex) {
          // No luck, try again.
          ENVOY_LOG_MISC(debug, "dynamic base id: {}", ex.what());
        }
      }

      if (restarter == nullptr) {
        throw EnvoyException("unable to select a dynamic base id");
      }

      restarter_.swap(restarter);
    } else {
      restarter_ = std::make_unique<Server::HotRestartImpl>(
          base_id, options_.restartEpoch(), options_.socketPath(), options_.socketMode());
    }

    // Write the base-id to the requested path whether we selected it
    // dynamically or not.
    if (!options_.baseIdPath().empty()) {
      std::ofstream base_id_out_file(options_.baseIdPath());
      if (!base_id_out_file) {
        ENVOY_LOG_MISC(critical, "cannot open base id output file {} for writing.",
                       options_.baseIdPath());
      } else {
        base_id_out_file << base_id;
      }
    }
  }
#else
  UNREFERENCED_PARAMETER(random_generator);
#endif

  if (restarter_ == nullptr) {
    restarter_ = std::make_unique<Server::HotRestartNopImpl>();
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
    auto response_headers = Http::ResponseHeaderMapImpl::create();
    std::string body;
    server_->admin().request(path_and_query_buf, method_buf, *response_headers, body);
    handler(*response_headers, body);
  });
}

MainCommon::MainCommon(int argc, const char* const* argv)
    : options_(argc, argv, &MainCommon::hotRestartVersion, spdlog::level::info),
      base_(options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
            std::make_unique<Random::RandomGeneratorImpl>(), platform_impl_.threadFactory(),
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

int MainCommon::main(int argc, char** argv, PostServerHook hook) {
#ifndef __APPLE__
  // absl::Symbolize mostly works without this, but this improves corner case
  // handling, such as running in a chroot jail.
  absl::InitializeSymbolizer(argv[0]);
#endif
  std::unique_ptr<Envoy::MainCommon> main_common;

  // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  try {
    main_common = std::make_unique<Envoy::MainCommon>(argc, argv);
    Envoy::Server::Instance* server = main_common->server();
    if (server != nullptr && hook != nullptr) {
      hook(*server);
    }
  } catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostics.
  return main_common->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace Envoy
