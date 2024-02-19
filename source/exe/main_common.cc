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
                       std::move(process_context), createFunction()) {}

bool MainCommonBase::run() {
  // Avoid returning from inside switch cases to minimize uncovered lines
  // while avoid gcc warnings by hitting the final return.
  bool ret = true;

  switch (options_.mode()) {
  case Server::Mode::Serve:
    runServer();
#ifdef ENVOY_ADMIN_FUNCTIONALITY
    terminateAdminRequests();
#endif
    break;
  case Server::Mode::Validate:
    ret = Server::validateConfig(
        options_, Network::Utility::getLocalAddress(options_.localAddressIpVersion()),
        component_factory_, platform_impl_->threadFactory(), platform_impl_->fileSystem(),
        process_context_ ? ProcessContextOptRef(std::ref(*process_context_)) : absl::nullopt);
    break;
  case Server::Mode::InitOnly:
    PERF_DUMP();
    break;
  default:
    ret = false;
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

namespace {

class AdminResponseImpl : public MainCommonBase::AdminResponse {
public:
  using CleanupFn = std::function<void(AdminResponseImpl*)>;

  AdminResponseImpl(Server::Instance& server, absl::string_view path, absl::string_view method,
                    CleanupFn cleanup)
      : server_(server), opt_admin_(server.admin()), cleanup_(cleanup) {
    request_headers_->setMethod(method);
    request_headers_->setPath(path);
  }

  ~AdminResponseImpl() {
    // MainCommonBase::response_set_ holds a raw pointer to all outstanding
    // responses (not a shared_ptr). So when destructing the response
    // we must call cleanup on MainCommonBase if this has not already
    // occurred.
    //
    // Note it's also possible for MainCommonBase to be deleted before
    // AdminResponseImpl, in which case it will use its response_set_
    // to call terminate, so we'll track that here and skip calling
    // cleanup_ in that case.
    if (!terminated_) {
      detaching_ = true;

      // If there is a terminate/destruct race, calling cleanup_ below
      // will lock MainCommonBase::mutex_, which is being held by
      // terminateAdminRequests, so will block here until all the
      // terminate calls are made. Once terminateAdminRequests
      // release its lock, cleanup_ will return and the object
      // can be safely destructed.
      cleanup_(this); // lock in MainCommonBase
    }
  }

  void getHeaders(HeadersFn fn) override {
    // First check for cancelling or termination.
    {
      absl::MutexLock lock(&mutex_);
      ASSERT(headers_fn_ == nullptr);
      if (cancelled_) {
        // We do not call callbacks after cancel().
        return;
      }
      headers_fn_ = fn;
      if (terminated_ || !opt_admin_) {
        sendErrorLockHeld();
        return;
      }
    }
    server_.dispatcher().post([this, response = shared_from_this()]() { requestHeaders(); });
  }

  void nextChunk(BodyFn fn) override {
    // Note the caller may race a call to nextChunk with the server being
    // terminated.
    {
      absl::MutexLock lock(&mutex_);
      ASSERT(body_fn_ == nullptr);
      body_fn_ = fn;
      if (terminated_ || cancelled_ || !opt_admin_) {
        sendAbortChunkLockHeld();
        return;
      }
    }

    // Note that nextChunk may be called from any thread -- it's the callers choice,
    // including the Envoy main thread, which would occur if the caller initiates
    // the request of a chunk upon receipt of the previous chunk.
    //
    // In that case it may race against the AdminResponse object being deleted,
    // in which case the callbacks, held in a shared_ptr, will be cancelled
    // from the destructor. If that happens *before* we post to the main thread,
    // we will just skip and never call fn.
    server_.dispatcher().post([this, response = shared_from_this()]() { requestNextChunk(); });
  }

  // Called by the user if it is not longer interested in the result of the
  // admin request. After calling cancel() the caller must not call nextChunk or
  // getHeaders.
  void cancel() override {
    absl::MutexLock lock(&mutex_);
    cancelled_ = true;
    headers_fn_ = nullptr;
    body_fn_ = nullptr;
  }

  // Called from MainCommonBase::terminateAdminRequests when the Envoy server
  // terminates. After this is called, the caller may need to complete the
  // admin response, and so calls to getHeader and nextChunk remain valid,
  // resulting in 503 and an empty body.
  void terminate() override {
    ASSERT_IS_MAIN_OR_TEST_THREAD();

    // To handle a potential race of Envoy terminate() destruction, we set
    // detaching_ first in the destructor and check here to avoid writing to the
    // object as it is being destroyed. If terminate() locks first, the
    // destructor will block on it and then early exit. If the destructor hits
    // during the loop in terminateAdminRequests, it will first set
    // detaching_=true, and then call MainCommonBase::detachResponse which will
    // acquire MainCommonBase::mutex_, held by terminateAdminRequests. This
    // will allow the loop to complete cleanly before detachResponse attempts
    // to mutate the set.
    absl::MutexLock lock(&mutex_);
    if (detaching_) {
      return;
    }
    terminated_ = true;
    sendErrorLockHeld();
    sendAbortChunkLockHeld();
  }

private:
  void requestHeaders() {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    {
      absl::MutexLock lock(&mutex_);
      if (cancelled_ || terminated_) {
        return;
      }
    }
    Server::AdminFilter filter(*opt_admin_);
    filter.decodeHeaders(*request_headers_, false);
    request_ = opt_admin_->makeRequest(filter);
    code_ = request_->start(*response_headers_);
    {
      absl::MutexLock lock(&mutex_);
      if (headers_fn_ != nullptr && !cancelled_) {
        Server::Utility::populateFallbackResponseHeaders(code_, *response_headers_);
        headers_fn_(code_, *response_headers_);
        headers_fn_ = nullptr;
      }
    }
  }

  void requestNextChunk() {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    {
      absl::MutexLock lock(&mutex_);
      if (cancelled_ || terminated_) {
        return;
      }
    }
    while (response_.length() == 0 && more_data_) {
      more_data_ = request_->nextChunk(response_);
    }
    {
      absl::MutexLock lock(&mutex_);
      if (sent_end_stream_ || cancelled_ || terminated_) {
        return;
      }
      sent_end_stream_ = !more_data_;
      body_fn_(response_, more_data_);
      ASSERT(response_.length() == 0);
      body_fn_ = nullptr;
    }
  }

  void sendAbortChunkLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    if (!sent_end_stream_ && body_fn_ != nullptr) {
      response_.drain(response_.length());
      body_fn_(response_, false);
      sent_end_stream_ = true;
    }
    body_fn_ = nullptr;
  }

  void sendErrorLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    if (headers_fn_ != nullptr) {
      code_ = Http::Code::InternalServerError;
      Server::Utility::populateFallbackResponseHeaders(code_, *response_headers_);
      headers_fn_(code_, *response_headers_);
      headers_fn_ = nullptr;
    }
  }

  Server::Instance& server_;
  OptRef<Server::Admin> opt_admin_;
  Buffer::OwnedImpl response_;
  Http::Code code_;
  Server::Admin::RequestPtr request_;
  CleanupFn cleanup_;
  Http::RequestHeaderMapPtr request_headers_{Http::RequestHeaderMapImpl::create()};
  Http::ResponseHeaderMapPtr response_headers_{Http::ResponseHeaderMapImpl::create()};
  bool more_data_ = true;

  // True if cancel() was explicitly called by the user; headers and body
  // callbacks are never called after cancel().
  bool cancelled_ ABSL_GUARDED_BY(mutex_) = false;

  // True if the Envoy server has stopped running its main loop. Headers
  // and boxy callbacks are called after this.
  bool terminated_ ABSL_GUARDED_BY(mutex_) = false;
  bool detaching_ ABSL_GUARDED_BY(mutex_) = false;

  // bool sent_headers_ ABSL_GUARDED_BY(mutex_) = false;
  bool sent_end_stream_ ABSL_GUARDED_BY(mutex_) = false;
  HeadersFn headers_fn_ ABSL_GUARDED_BY(mutex_);
  BodyFn body_fn_ ABSL_GUARDED_BY(mutex_);
  absl::Mutex mutex_;
};

} // namespace

void MainCommonBase::terminateAdminRequests() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  absl::MutexLock lock(&mutex_);
  accepting_admin_requests_ = false;
  for (AdminResponse* response : response_set_) {
    // Consider the possibility of response being deleted due to its creator
    // dropping its last reference right here. From its destructor it will call
    // detachResponse(), which is mutex-ed against this loop, so before the
    // memory becomes invalid, the call to terminate will complete.
    response->terminate();
  }
  response_set_.clear();
}

void MainCommonBase::detachResponse(AdminResponse* response) {
  absl::MutexLock lock(&mutex_);

  // In a race between ~AdminResponseImpl and terminateAdminRequests,
  // the response_set_ may already have been cleared and the erasure
  // below will not have any effect; this is OK.
  int erased = response_set_.erase(response);
  ASSERT(erased == 1 || !accepting_admin_requests_);
}

MainCommonBase::AdminResponseSharedPtr
MainCommonBase::adminRequest(absl::string_view path_and_query, absl::string_view method) {
  auto response = std::make_shared<AdminResponseImpl>(
      *server(), path_and_query, method,
      [this](AdminResponse* response) { detachResponse(response); });
  absl::MutexLock lock(&mutex_);
  if (accepting_admin_requests_) {
    response_set_.insert(response.get());
  } else {
    response->terminate();
  }
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
