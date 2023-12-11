#include "source/exe/stripped_main_base.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <new>

#include "envoy/config/listener/v3/listener.pb.h"

#include "source/common/common/compiler_requirements.h"
#include "source/common/common/logger.h"
#include "source/common/common/perf_annotation.h"
#include "source/common/network/utility.h"
#include "source/common/stats/thread_local_store.h"
#include "source/exe/platform_impl.h"
#include "source/server/drain_manager_impl.h"
#include "source/server/hot_restart_nop_impl.h"
#include "source/server/listener_hooks.h"
#include "source/server/options_impl_base.h"
#include "source/server/server.h"

#include "absl/debugging/symbolize.h"
#include "absl/strings/str_split.h"

#ifdef ENVOY_HOT_RESTART
#include "source/server/hot_restart_impl.h"
#endif

namespace Envoy {

Server::DrainManagerPtr ProdComponentFactory::createDrainManager(Server::Instance& server) {
  // The global drain manager only triggers on listener modification, which effectively is
  // hot restart at the global level. The per-listener drain managers decide whether to
  // to include /healthcheck/fail status.
  return std::make_unique<Server::DrainManagerImpl>(
      server, envoy::config::listener::v3::Listener::MODIFY_ONLY, server.dispatcher());
}

Runtime::LoaderPtr ProdComponentFactory::createRuntime(Server::Instance& server,
                                                       Server::Configuration::Initial& config) {
  return Server::InstanceUtil::createRuntime(server, config);
}

StrippedMainBase::StrippedMainBase(const Server::Options& options, Event::TimeSystem& time_system,
                                   ListenerHooks& listener_hooks,
                                   Server::ComponentFactory& component_factory,
                                   std::unique_ptr<Server::Platform> platform_impl,
                                   std::unique_ptr<Random::RandomGenerator>&& random_generator,
                                   std::unique_ptr<ProcessContext> process_context,
                                   CreateInstanceFunction createInstance)
    : platform_impl_(std::move(platform_impl)), options_(options),
      component_factory_(component_factory), stats_allocator_(symbol_table_) {
  // Process the option to disable extensions as early as possible,
  // before we do any configuration loading.
  OptionsImplBase::disableExtensions(options.disabledExtensions());

  // Enable core dumps as early as possible.
  if (options_.coreDumpEnabled()) {
    const auto ret = platform_impl_->enableCoreDump();
    if (ret) {
      ENVOY_LOG_MISC(info, "core dump enabled");
    } else {
      ENVOY_LOG_MISC(warn, "failed to enable core dump");
    }
  }

  switch (options_.mode()) {
  case Server::Mode::InitOnly:
  case Server::Mode::Serve: {
    configureHotRestarter(*random_generator);

    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    Thread::BasicLockable& log_lock = restarter_->logLock();
    Thread::BasicLockable& access_log_lock = restarter_->accessLogLock();
    logging_context_ = std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(),
                                                         log_lock, options_.logFormatEscaped(),
                                                         options_.enableFineGrainLogging());

    configureComponentLogLevels();

    // Provide consistent behavior for out-of-memory, regardless of whether it occurs in a
    // try/catch block or not.
    std::set_new_handler([]() { PANIC("out of memory"); });

    stats_store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(stats_allocator_);

    server_ = createInstance(*init_manager_, options_, time_system, listener_hooks, *restarter_,
                             *stats_store_, access_log_lock, component_factory,
                             std::move(random_generator), *tls_, platform_impl_->threadFactory(),
                             platform_impl_->fileSystem(), std::move(process_context), nullptr);
    break;
  }
  case Server::Mode::Validate:
    restarter_ = std::make_unique<Server::HotRestartNopImpl>();
    logging_context_ =
        std::make_unique<Logger::Context>(options_.logLevel(), options_.logFormat(),
                                          restarter_->logLock(), options_.logFormatEscaped());
    process_context_ = std::move(process_context);
    break;
  }
}

void StrippedMainBase::configureComponentLogLevels() {
  for (auto& component_log_level : options_.componentLogLevels()) {
    Logger::Logger* logger_to_change = Logger::Registry::logger(component_log_level.first);
    ASSERT(logger_to_change);
    logger_to_change->setLevel(component_log_level.second);
  }
}

void StrippedMainBase::configureHotRestarter(Random::RandomGenerator& random_generator) {
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

        TRY_ASSERT_MAIN_THREAD {
          restarter = std::make_unique<Server::HotRestartImpl>(base_id, 0, options_.socketPath(),
                                                               options_.socketMode());
        }
        END_TRY
        CATCH(Server::HotRestartDomainSocketInUseException & ex, {
          // No luck, try again.
          ENVOY_LOG_MISC(debug, "dynamic base id: {}", ex.what());
        });
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

} // namespace Envoy
