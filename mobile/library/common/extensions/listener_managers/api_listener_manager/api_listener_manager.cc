#include "library/common/extensions/listener_managers/api_listener_manager/api_listener_manager.h"

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/listener.h"
#include "envoy/server/api_listener.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "absl/synchronization/notification.h"

namespace Envoy {
namespace Server {

ApiListenerWorker::ApiListenerWorker(Instance& server)
    : server_(server), dispatcher_(server.api().allocateDispatcher("api_listener_worker")),
      provisional_dispatcher_(std::make_unique<Event::ProvisionalDispatcher>()) {
  server_.threadLocal().registerThread(*dispatcher_, false);
  provisional_dispatcher_->drain(*dispatcher_);
}

ApiListenerWorker::~ApiListenerWorker() {}

void ApiListenerWorker::addListener(ApiListenerOptRef api_listener, std::function<void()> cb) {
  dispatcher_->post([this, api_listener, cb]() {
    api_listener_ = api_listener;
    if (api_listener_.has_value()) {
      auto api_listener_impl = api_listener_->get().createHttpApiListener(*dispatcher_);
      ASSERT(api_listener_impl != nullptr);
      http_client_ = std::make_unique<Http::Client>(
          std::move(api_listener_impl), *provisional_dispatcher_,
          server_.serverFactoryContext().scope(), server_.api().randomGenerator());
      ENVOY_LOG_MISC(info, "Created API listener.");
      if (shutdown_notification_.HasBeenNotified()) {
        ENVOY_LOG_MISC(info, "Shutting down API listener.");
        http_client_->shutdownApiListener();
      }
    }
    if (cb) {
      server_.dispatcher().post([cb]() { cb(); });
    }
  });
}

void ApiListenerWorker::start(OptRef<GuardDog> guard_dog, const std::function<void()>& cb) {
  ASSERT(!thread_);

  Thread::Options options{absl::StrCat("wrk:", dispatcher_->name())};
  thread_ = server_.api().threadFactory().createThread(
      [this, guard_dog, cb]() -> void { threadRoutine(guard_dog, cb); }, options);
}

void ApiListenerWorker::initializeStats(Stats::Scope& scope) {
  if (dispatcher_) {
    dispatcher_->initializeStats(scope);
  }
}

void ApiListenerWorker::stop() {
  if (thread_) {
    shutdown_notification_.WaitForNotification();
    ENVOY_LOG_MISC(info, "Listener has been shutdown, exiting the event loop.");
    dispatcher_->exit();
    thread_->join();
    thread_.reset();
  }
}

void ApiListenerWorker::stopListener() {
  dispatcher_->post([this]() {
    if (http_client_) {
      http_client_->shutdownApiListener();
    }
    shutdown_notification_.Notify();
  });
}

void ApiListenerWorker::threadRoutine(OptRef<GuardDog> guard_dog, const std::function<void()>& cb) {
  if (cb) {
    dispatcher_->post(cb);
  }
  if (guard_dog.has_value()) {
    dispatcher_->post([this, guard_dog]() {
      watch_dog_ = guard_dog->createWatchDog(server_.api().threadFactory().currentThreadId(),
                                             dispatcher_->name(), *dispatcher_);
    });
  }

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  dispatcher_->shutdown();
  ENVOY_LOG_MISC(info, "ApiListenerWorker dispatcher exited");
  if (guard_dog.has_value() && watch_dog_) {
    guard_dog->stopWatching(watch_dog_);
  }

  watch_dog_.reset();
}

ApiListenerManagerImpl::ApiListenerManagerImpl(Instance& server, bool use_worker_thread)
    : server_(server),
      worker_(use_worker_thread ? std::make_unique<ApiListenerWorker>(server) : nullptr) {}

ApiListenerManagerImpl::~ApiListenerManagerImpl() {
  if (worker_) {
    stopWorkers();
  }
}

absl::StatusOr<bool>
ApiListenerManagerImpl::addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                                            const std::string&, bool added_via_api) {
  ENVOY_LOG(debug, "Creating API listener manager");
  std::string name;
  if (!config.name().empty()) {
    name = config.name();
  } else {
    // TODO (soulxu): The random uuid name is bad for logging. We can use listening addresses in
    // the log to improve that.
    name = server_.api().randomGenerator().uuid();
  }

  // TODO(junr03): currently only one ApiListener can be installed via bootstrap to avoid having to
  // build a collection of listeners, and to have to be able to warm and drain the listeners. In the
  // future allow multiple ApiListeners, and allow them to be created via LDS as well as bootstrap.
  if (config.has_api_listener()) {
    if (config.has_internal_listener()) {
      return absl::InvalidArgumentError(fmt::format(
          "error adding listener named '{}': api_listener and internal_listener cannot be both set",
          name));
    }
    if (!api_listener_ && !added_via_api) {
      auto* api_listener_factory =
          Registry::FactoryRegistry<Server::ApiListenerFactory>::getFactory(
              "envoy.http_api_listener");
      if (api_listener_factory == nullptr) {
        return absl::InvalidArgumentError(fmt::format(
            "error adding listener named '{}': missing the API listener extension", name));
      }
      auto listener_or_error = api_listener_factory->create(config, server_, config.name());
      RETURN_IF_NOT_OK(listener_or_error.status());
      api_listener_ = std::move(listener_or_error.value());
      return true;
    } else {
      ENVOY_LOG(warn, "listener {} can not be added because currently only one ApiListener is "
                      "allowed, and it can only be added via bootstrap configuration");
      return false;
    }
  }
  return false;
}

absl::Status ApiListenerManagerImpl::startWorkers(OptRef<GuardDog> guard_dog,
                                                  std::function<void()> callback) {
  if (!worker_) {
    if (callback) {
      callback();
    }
    return absl::OkStatus();
  }

  if (worker_started_.load()) {
    return absl::OkStatus();
  }

  ASSERT(api_listener_ != nullptr, "api_listener_ is null in startWorkers!");
  worker_->addListener(api_listener_ ? ApiListenerOptRef(std::ref(*api_listener_)) : absl::nullopt,
                       callback);
  absl::Notification worker_started_notification;
  worker_->start(guard_dog, [&worker_started_notification, this]() {
    worker_started_.store(true);
    worker_started_notification.Notify();
  });
  worker_started_notification.WaitForNotification();
  ENVOY_LOG_MISC(info, "Worker thread has been started");

  // The client should be available at this point.
  worker_->httpClient();

  return absl::OkStatus();
}

void ApiListenerManagerImpl::stopListeners(StopListenersType,
                                           const Network::ExtraShutdownListenerOptions&) {
  worker_->stopListener();
}

void ApiListenerManagerImpl::stopWorkers() {
  if (worker_started_.load()) {
    worker_->stop();
  }
}

REGISTER_FACTORY(ApiListenerManagerFactoryImpl, ListenerManagerFactory);

} // namespace Server
} // namespace Envoy
