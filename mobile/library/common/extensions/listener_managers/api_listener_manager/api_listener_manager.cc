#include "library/common/extensions/listener_managers/api_listener_manager/api_listener_manager.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "envoy/server/listener_manager.h"
#include "source/common/common/logger.h"

#include "absl/synchronization/notification.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/listener.h"
#include "envoy/server/api_listener.h"
#include "library/common/engine_types.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Server {

ApiListenerWorker::ApiListenerWorker(Instance& server)
    : server_(server), dispatcher_(server.api().allocateDispatcher("api_listener_worker")),
      provisional_dispatcher_(std::make_unique<Event::ProvisionalDispatcher>()) {
  server_.threadLocal().registerThread(*dispatcher_, false);
  provisional_dispatcher_->drain(*dispatcher_);
}

ApiListenerWorker::~ApiListenerWorker() {}

void ApiListenerWorker::addListener(const std::string& name,
                                    std::shared_ptr<ApiListener> api_listener,
                                    std::function<void()> cb) {
  dispatcher_->post([this, name, api_listener, cb]() {
    if (api_listener != nullptr) {
      auto it = http_clients_.find(name);
      if (it != http_clients_.end()) {
        it->second->shutdownApiListener();
        http_clients_.erase(it);
      }
      auto api_listener_impl = api_listener->createHttpApiListener(*dispatcher_);
      ASSERT(api_listener_impl != nullptr);
      http_clients_[name] = std::make_unique<Http::Client>(
          std::move(api_listener_impl), *provisional_dispatcher_,
          server_.serverFactoryContext().scope(), server_.api().randomGenerator());
      ENVOY_LOG_MISC(info, "Created API listener {}.", name);
      if (shutdown_notification_.HasBeenNotified()) {
        ENVOY_LOG_MISC(info, "Shutting down API listener {}.", name);
        http_clients_[name]->shutdownApiListener();
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
    if (!shutdown_notification_.HasBeenNotified()) {
      stopListener();
    }
    shutdown_notification_.WaitForNotification();
    ENVOY_LOG_MISC(info, "Listener has been shutdown, exiting the event loop.");
    dispatcher_->exit();
    thread_->join();
    thread_.reset();
  }
}

void ApiListenerWorker::stopListener() {
  if (stop_posted_.exchange(true)) {
    return;
  }
  dispatcher_->post([this]() {
    for (auto& [name, client] : http_clients_) {
      if (client) {
        client->shutdownApiListener();
      }
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

ApiListenerOptRef ApiListenerManagerImpl::apiListener() {
  auto it = api_listeners_.find(DEFAULT_API_LISTENER_NAME);
  if (it != api_listeners_.end()) {
    return ApiListenerOptRef(std::ref(*it->second));
  }
  if (api_listeners_.empty()) {
    return absl::nullopt;
  }
  // Fall back to the first listener if the default one is not found but we have
  // one.
  return ApiListenerOptRef(std::ref(*api_listeners_.begin()->second));
}

ApiListenerOptRef ApiListenerManagerImpl::apiListener(absl::string_view name) {
  auto it = api_listeners_.find(name);
  if (it != api_listeners_.end()) {
    return ApiListenerOptRef(std::ref(*it->second));
  }
  return absl::nullopt;
}

Http::Client* ApiListenerManagerImpl::httpClient(const std::string& name) {
  if (worker_) {
    return worker_->httpClient(name);
  }
  ASSERT(api_listeners_.size() <= 1);
  if (!name.empty() && !api_listeners_.empty()) {
    if (name != api_listeners_.begin()->first) {
      return nullptr;
    }
  }
  return http_client_.get();
}

absl::StatusOr<bool>
ApiListenerManagerImpl::addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                                            const std::string&, bool added_via_api) {
  ENVOY_LOG(debug, "Creating API listener manager");
  std::string name;
  if (!config.name().empty()) {
    name = config.name();
  } else {
    name = server_.api().randomGenerator().uuid();
  }

  if (config.has_api_listener()) {
    if (config.has_internal_listener()) {
      return absl::InvalidArgumentError(fmt::format(
          "error adding listener named '{}': api_listener and internal_listener cannot be both set",
          name));
    }
    if (!added_via_api) {
      auto* api_listener_factory =
          Registry::FactoryRegistry<Server::ApiListenerFactory>::getFactory(
              "envoy.http_api_listener");
      if (api_listener_factory == nullptr) {
        return absl::InvalidArgumentError(fmt::format(
            "error adding listener named '{}': missing the API listener extension", name));
      }
      auto listener_or_error = api_listener_factory->create(config, server_, name);
      RETURN_IF_NOT_OK(listener_or_error.status());

      // TODO(junr03): support warming and draining of API listeners when
      // multiple are installed.
      bool is_update = (api_listeners_.find(name) != api_listeners_.end());

      // If there is no worker thread, we only support a single API listener.
      // We check if this is an update to the existing one, or if we are trying
      // to add a new one when we already have one.
      if (!worker_ && !is_update && !api_listeners_.empty()) {
        return absl::InvalidArgumentError(
            "Multiple ApiListeners are not supported when worker thread is "
            "disabled.");
      }

      if (is_update && worker_started_ && !worker_) {
        if (http_client_) {
          http_client_->shutdownApiListener();
          http_client_.reset();
        }
      }

      api_listeners_[name] = std::move(listener_or_error.value());

      if (worker_started_) {
        if (!worker_) {
          setupClient();
        } else {
          worker_->addListener(name, api_listeners_[name], nullptr);
        }
      }
      return true;
    } else {
      ENVOY_LOG(warn,
                "listener {} can not be added because it can only be added via "
                "bootstrap configuration",
                name);
      return false;
    }
  }
  return false;
}

absl::Status ApiListenerManagerImpl::startWorkers(OptRef<GuardDog> guard_dog,
                                                  std::function<void()> callback) {
  if (worker_started_.exchange(true)) {
    if (callback) {
      callback();
    }
    return absl::OkStatus();
  }

  if (!worker_) {
    setupClient();
    if (provisional_dispatcher_) {
      provisional_dispatcher_->drain(server_.dispatcher());
    }
    if (callback) {
      callback();
    }
    return absl::OkStatus();
  }

  if (api_listeners_.empty()) {
    if (callback) {
      callback();
    }
    return absl::OkStatus();
  }

  size_t remaining = api_listeners_.size();
  size_t i = 0;
  for (auto& [name, listener] : api_listeners_) {
    bool is_last = (i == remaining - 1);
    worker_->addListener(name, listener, is_last ? callback : nullptr);
    i++;
  }

  absl::Notification worker_started_notification;
  worker_->start(guard_dog,
                 [&worker_started_notification]() { worker_started_notification.Notify(); });
  worker_started_notification.WaitForNotification();
  ENVOY_LOG_MISC(info, "Worker thread has been started");

  return absl::OkStatus();
}

void ApiListenerManagerImpl::stopListeners(StopListenersType,
                                           const Network::ExtraShutdownListenerOptions&) {
  if (worker_) {
    worker_->stopListener();
  } else {
    if (http_client_) {
      http_client_->shutdownApiListener();
    }
  }
}

void ApiListenerManagerImpl::stopWorkers() {
  if (worker_started_.load() && worker_) {
    worker_->stop();
  }
}

void ApiListenerManagerImpl::setupClient() {
  ASSERT(!worker_);
  ASSERT(api_listeners_.size() <= 1);
  if (api_listeners_.empty()) {
    return;
  }
  auto& name = api_listeners_.begin()->first;
  if (!provisional_dispatcher_) {
    provisional_dispatcher_ = std::make_unique<Event::ProvisionalDispatcher>();
  }
  auto api_listener_impl = api_listeners_[name]->createHttpApiListener(server_.dispatcher());
  http_client_ = std::make_unique<Http::Client>(
      std::move(api_listener_impl), *provisional_dispatcher_,
      server_.serverFactoryContext().scope(), server_.api().randomGenerator());
}

REGISTER_FACTORY(ApiListenerManagerFactoryImpl, ListenerManagerFactory);

} // namespace Server
} // namespace Envoy
