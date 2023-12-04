#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/platform.h"

#include "source/common/common/thread.h"
#include "source/common/event/real_time_system.h"
#include "source/common/grpc/google_grpc_context.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/exe/process_wide.h"
#include "source/server/listener_hooks.h"
#include "source/server/server.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "source/common/signal/signal_action.h"
#include "source/exe/terminate_handler.h"
#endif

namespace Envoy {

class ProdComponentFactory : public Server::ComponentFactory {
public:
  // Server::DrainManagerFactory
  Server::DrainManagerPtr createDrainManager(Server::Instance& server) override;
  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override;
};

// This is the common main between Envoy and Envoy mobile.
// It is stripped down to functionality required by Envoy Mobile: anything
// server-specific should live in MainCommonBase or MainCommon which remain
// separate for legacy reasons.
class StrippedMainBase {
public:
  using CreateInstanceFunction = std::function<std::unique_ptr<Server::Instance>(
      Init::Manager& init_manager, const Server::Options& options, Event::TimeSystem& time_system,
      ListenerHooks& hooks, Server::HotRestart& restarter, Stats::StoreRoot& store,
      Thread::BasicLockable& access_log_lock, Server::ComponentFactory& component_factory,
      Random::RandomGeneratorPtr&& random_generator, ThreadLocal::Instance& tls,
      Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system,
      std::unique_ptr<ProcessContext> process_context,
      Buffer::WatermarkFactorySharedPtr watermark_factory)>;

  static std::string hotRestartVersion(bool hot_restart_enabled);

  // Consumer must guarantee that all passed references are alive until this object is
  // destructed.
  StrippedMainBase(const Server::Options& options, Event::TimeSystem& time_system,
                   ListenerHooks& listener_hooks, Server::ComponentFactory& component_factory,
                   std::unique_ptr<Server::Platform> platform_impl,
                   std::unique_ptr<Random::RandomGenerator>&& random_generator,
                   std::unique_ptr<ProcessContext> process_context,
                   CreateInstanceFunction createInstance);

  void runServer() {
    ASSERT(options_.mode() == Server::Mode::Serve);
    server_->run();
  }

  // Will be null if options.mode() == Server::Mode::Validate
  Server::Instance* server() { return server_.get(); }

protected:
  std::unique_ptr<Server::Platform> platform_impl_;
  ProcessWide process_wide_; // Process-wide state setup/teardown (excluding grpc).
  // We instantiate this class regardless of ENVOY_GOOGLE_GRPC, to avoid having
  // an ifdef in a header file exposed in a C++ library. It is too easy to have
  // the ifdef be inconsistent across build-system boundaries.
  Grpc::GoogleGrpcContext google_grpc_context_;
  const Envoy::Server::Options& options_;
  Server::ComponentFactory& component_factory_;
  Stats::SymbolTableImpl symbol_table_;
  Stats::AllocatorImpl stats_allocator_;

  ThreadLocal::InstanceImplPtr tls_;
  std::unique_ptr<Server::HotRestart> restarter_;
  Stats::ThreadLocalStoreImplPtr stats_store_;
  std::unique_ptr<Logger::Context> logging_context_;
  std::unique_ptr<Init::Manager> init_manager_{std::make_unique<Init::ManagerImpl>("Server")};
  std::unique_ptr<Server::Instance> server_;

  // Only used for validation mode
  std::unique_ptr<ProcessContext> process_context_;

private:
  void configureComponentLogLevels();
  void configureHotRestarter(Random::RandomGenerator& random_generator);

  // Declaring main thread here allows custom integrations to instantiate
  // StrippedMainBase directly, with environment-specific dependency injection.
  // Note that MainThread must also be declared in MainCommon.
  Thread::MainThread main_thread_;
};

} // namespace Envoy
