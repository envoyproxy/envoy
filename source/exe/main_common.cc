#include "source/exe/main_common.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <new>

#include "envoy/config/listener/v3/listener.pb.h"

#include "source/common/common/compiler_requirements.h"
#include "source/common/common/logger.h"
#include "source/common/common/perf_annotation.h"
#include "source/common/common/thread.h"
#include "source/common/network/utility.h"
#include "source/common/stats/thread_local_store.h"
#include "source/exe/platform_impl.h"
#include "source/server/config_validation/server.h"
#include "source/server/drain_manager_impl.h"
#include "source/server/hot_restart_nop_impl.h"
#include "source/server/instance_impl.h"
#include "source/server/listener_hooks.h"
#include "source/server/options_impl_base.h"

#include "absl/debugging/symbolize.h"
#include "absl/strings/str_split.h"

#ifdef ENVOY_HOT_RESTART
#include "source/server/hot_restart_impl.h"
#endif

namespace Envoy {

StrippedMainBase::CreateInstanceFunction createFunction() {
  return
      [](Init::Manager& init_manager, const Server::Options& options,
         Event::TimeSystem& time_system, ListenerHooks& hooks, Server::HotRestart& restarter,
         Stats::StoreRoot& store, Thread::BasicLockable& access_log_lock,
         Server::ComponentFactory& component_factory, Random::RandomGeneratorPtr&& random_generator,
         ThreadLocal::Instance& tls, Thread::ThreadFactory& thread_factory,
         Filesystem::Instance& file_system, std::unique_ptr<ProcessContext> process_context,
         Buffer::WatermarkFactorySharedPtr watermark_factory) {
        auto local_address = Network::Utility::getLocalAddress(options.localAddressIpVersion());
        auto server = std::make_unique<Server::InstanceImpl>(
            init_manager, options, time_system, hooks, restarter, store, access_log_lock,
            std::move(random_generator), tls, thread_factory, file_system,
            std::move(process_context), watermark_factory);
        server->initialize(local_address, component_factory);
        return server;
      };
}

MainCommonBase::MainCommonBase(const Server::Options& options, Event::TimeSystem& time_system,
                               ListenerHooks& listener_hooks,
                               Server::ComponentFactory& component_factory,
                               std::unique_ptr<Server::Platform> platform_impl,
                               std::unique_ptr<Random::RandomGenerator>&& random_generator,
                               std::unique_ptr<ProcessContext> process_context)
    : StrippedMainBase(options, time_system, listener_hooks, component_factory,
                       std::move(platform_impl), std::move(random_generator),
                       std::move(process_context), createFunction())
#ifdef ENVOY_ADMIN_FUNCTIONALITY
      ,
      shared_response_set_(std::make_shared<AdminResponse::PtrSet>())
#endif
{
}

bool MainCommonBase::run() {
  // Avoid returning from inside switch cases to minimize uncovered lines
  // while avoiding gcc warnings by hitting the final return.
  bool ret = false;

  switch (options_.mode()) {
  case Server::Mode::Serve:
    runServer();
#ifdef ENVOY_ADMIN_FUNCTIONALITY
    shared_response_set_->terminateAdminRequests();
#endif
    ret = true;
    break;
  case Server::Mode::Validate:
    ret = Server::validateConfig(
        options_, Network::Utility::getLocalAddress(options_.localAddressIpVersion()),
        component_factory_, platform_impl_->threadFactory(), platform_impl_->fileSystem(),
        process_context_ ? ProcessContextOptRef(std::ref(*process_context_)) : absl::nullopt);
    break;
  case Server::Mode::InitOnly:
    PERF_DUMP();
    ret = true;
    break;
  }
  return ret;
}

#ifdef ENVOY_ADMIN_FUNCTIONALITY

// This request variant buffers the entire response in one string. New uses
// should opt for the streaming version below, where an AdminResponse object
// is created and used to stream data with flow-control.
void MainCommonBase::adminRequest(absl::string_view path_and_query, absl::string_view method,
                                  const AdminRequestFn& handler) {
  std::string path_and_query_buf = std::string(path_and_query);
  std::string method_buf = std::string(method);
  server_->dispatcher().post([this, path_and_query_buf, method_buf, handler]() {
    auto response_headers = Http::ResponseHeaderMapImpl::create();
    std::string body;
    if (server_->admin()) {
      server_->admin()->request(path_and_query_buf, method_buf, *response_headers, body);
    }
    handler(*response_headers, body);
  });
}

AdminResponseSharedPtr MainCommonBase::adminRequest(absl::string_view path_and_query,
                                                    absl::string_view method) {
  auto response =
      std::make_shared<AdminResponse>(*server(), path_and_query, method, shared_response_set_);
  shared_response_set_->attachResponse(response.get());
  return response;
}
#endif

MainCommon::MainCommon(const std::vector<std::string>& args)
    : options_(args, &MainCommon::hotRestartVersion, spdlog::level::info),
      base_(options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
            std::make_unique<PlatformImpl>(), std::make_unique<Random::RandomGeneratorImpl>(),
            nullptr) {}

MainCommon::MainCommon(int argc, const char* const* argv)
    : options_(argc, argv, &MainCommon::hotRestartVersion, spdlog::level::info),
      base_(options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
            std::make_unique<PlatformImpl>(), std::make_unique<Random::RandomGeneratorImpl>(),
            nullptr) {}

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
  Thread::MainThread main_thread;
  std::unique_ptr<Envoy::MainCommon> main_common;

  // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  TRY_ASSERT_MAIN_THREAD {
    main_common = std::make_unique<Envoy::MainCommon>(argc, argv);
    Envoy::Server::Instance* server = main_common->server();
    if (server != nullptr && hook != nullptr) {
      hook(*server);
    }
  }
  END_TRY
  catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  }
  catch (const Envoy::MalformedArgvException& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  catch (const Envoy::EnvoyException& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostics.
  return main_common->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace Envoy
