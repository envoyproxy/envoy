#include "source/exe/main_common.h"

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
                       std::move(process_context), createFunction()) {}

bool MainCommonBase::run() {
  switch (options_.mode()) {
  case Server::Mode::Serve:
    runServer();
    return true;
  case Server::Mode::Validate:
    return Server::validateConfig(
        options_, Network::Utility::getLocalAddress(options_.localAddressIpVersion()),
        component_factory_, platform_impl_->threadFactory(), platform_impl_->fileSystem(),
        process_context_ ? ProcessContextOptRef(std::ref(*process_context_)) : absl::nullopt);
  case Server::Mode::InitOnly:
    PERF_DUMP();
    return true;
  }
  return false; // for gcc.
}

#ifdef ENVOY_ADMIN_FUNCTIONALITY
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

namespace {
class AdminResponseImpl : public MainCommonBase::AdminResponse {
public:
  AdminResponseImpl(Server::Instance& server, absl::string_view path, absl::string_view method)
      : server_(server), opt_admin_(server.admin()) {
    request_headers_->setMethod(method);
    request_headers_->setPath(path);
  }

  void getHeaders(HeadersFn fn) override {
    if (!opt_admin_) {
      fn(Http::Code::InternalServerError, *response_headers_);
      return;
    }

    server_.dispatcher().post([this, fn]() {
      if (!sent_headers_.exchange(true)) {
        Server::AdminFilter filter(opt_admin_->createRequestFunction());
        filter.decodeHeaders(*request_headers_, false);
        request_ = opt_admin_->makeRequest(filter);
        code_ = request_->start(*response_headers_);
        Server::Utility::populateFallbackResponseHeaders(code_, *response_headers_);
        fn(code_, *response_headers_);
      }
    });
    if (server_.isShutdown()) {
      if (!sent_headers_.exchange(true)) {
        fn(Http::Code::InternalServerError, *response_headers_);
      }
    }
  }

  void nextChunk(BodyFn fn) override {
    server_.dispatcher().post([this, fn]() {
      while (response_.length() == 0 && more_data_) {
        more_data_ = request_->nextChunk(response_);
      }
      /*      Buffer::SliceDataPtr slice;
      absl::string_view chunk;
      if (response_.length() != 0) {
        slice = response_.extractMutableFrontSlice();
        absl::Span<uint8_t> data = slice->getMutableData();
        more_data_ |= response_.length() != 0;
        chunk = absl::string_view(
            reinterpret_cast<char*>(data.data()), data.size());
      } else {
        more_data_ = false;
      }
      sendChunk(fn, chunk, more_data_);
      */
      sendChunk(fn, response_, more_data_);
    });

    // We do not know if the post () worked.
    if (server_.isShutdown()) {
      Buffer::OwnedImpl error;
      error.add("server was shut down");
      sendChunk(fn, error, false);
    }
  }

  void sendChunk(BodyFn fn, Buffer::Instance& chunk, bool more_data) {
    {
      absl::MutexLock lock(&mutex_);
      if (sent_end_stream_) {
        return;
      }
      sent_end_stream_ = !more_data;
    }
    fn(chunk, more_data);
  }

private:
  Server::Instance& server_;
  OptRef<Server::Admin> opt_admin_;
  Buffer::OwnedImpl response_;
  Http::Code code_;
  Server::Admin::RequestPtr request_;
  Http::RequestHeaderMapPtr request_headers_{Http::RequestHeaderMapImpl::create()};
  Http::ResponseHeaderMapPtr response_headers_{Http::ResponseHeaderMapImpl::create()};
  bool more_data_{true};
  std::atomic<bool> sent_headers_{false};
  bool sent_end_stream_{false};
  absl::Mutex mutex_;
};
} // namespace

MainCommonBase::AdminResponsePtr MainCommonBase::adminRequest(absl::string_view path_and_query,
                                                              absl::string_view method) {
  return std::make_unique<AdminResponseImpl>(*server(), path_and_query, method);
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
