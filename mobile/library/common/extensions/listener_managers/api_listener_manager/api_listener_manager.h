#pragma once

#include <memory>

#include "absl/synchronization/notification.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.validate.h"

#include "source/server/listener_manager_factory.h"

#include "envoy/thread/thread.h"
#include "library/common/event/provisional_dispatcher.h"
#include "library/common/http/client.h"

namespace Envoy {
namespace Server {

class ApiListenerWorker {
public:
  ApiListenerWorker(Instance& server);
  ~ApiListenerWorker();

  void addListener(ApiListenerOptRef api_listener, std::function<void()> cb);
  void start(OptRef<GuardDog> guard_dog, const std::function<void()>& cb);
  void initializeStats(Stats::Scope& scope);
  void stop();
  void stopListener();

  Http::Client& httpClient() {
    RELEASE_ASSERT(http_client_ != nullptr, "http_client_ is null!");
    return *http_client_;
  }
  Event::Dispatcher& dispatcher() { return *dispatcher_; }

private:
  void threadRoutine(OptRef<GuardDog> guard_dog, const std::function<void()>& cb);

  Instance& server_;
  ApiListenerOptRef api_listener_;
  Thread::ThreadPtr thread_;
  Event::DispatcherPtr dispatcher_;
  // Wraps above dispatcher_ for the http client.
  std::unique_ptr<Event::ProvisionalDispatcher> provisional_dispatcher_;
  std::unique_ptr<Http::Client> http_client_;
  WatchDogSharedPtr watch_dog_;
  absl::Notification shutdown_notification_;
};

/**
 * Implementation of a lightweight ListenerManager for Envoy Mobile.
 * This does not handle downstream TCP / UDP connections but only the API listener.
 */
class ApiListenerManagerImpl : public ListenerManager, Logger::Loggable<Logger::Id::config> {
public:
  explicit ApiListenerManagerImpl(Instance& server, bool use_worker_thread = false);
  ~ApiListenerManagerImpl() override;

  // Server::ListenerManager
  absl::StatusOr<bool> addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                                           const std::string& version_info,
                                           bool added_via_api) override;
  void createLdsApi(const envoy::config::core::v3::ConfigSource&,
                    const xds::core::v3::ResourceLocator*) override {}
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners(ListenerState) override {
    return {};
  }
  uint64_t numConnections() const override { return 0; }
  bool removeListener(const std::string&) override { return true; }
  absl::Status startWorkers(OptRef<GuardDog> guard_dog, std::function<void()> callback) override;
  void stopListeners(StopListenersType, const Network::ExtraShutdownListenerOptions&) override;
  void stopWorkers() override;
  void beginListenerUpdate() override {}
  void endListenerUpdate(FailureStates&&) override {}
  bool isWorkerStarted() override { return worker_started_.load(); }
  Http::Context& httpContext() { return server_.httpContext(); }
  ApiListenerOptRef apiListener() override {
    return api_listener_ ? ApiListenerOptRef(std::ref(*api_listener_)) : absl::nullopt;
  }
  ListenerUpdateCallbacksHandlePtr addListenerUpdateCallbacks(ListenerUpdateCallbacks&) override {
    return std::make_unique<ListenerUpdateCallbacksNopHandle>();
  }

  Http::Client& httpClient() {
    ASSERT(worker_ != nullptr, "httpClient() called when worker_ is null!");
    return worker_->httpClient();
  }

  Event::Dispatcher& httpClientDispatcher() {
    ASSERT(worker_ != nullptr, "httpClientDispatcher() called when worker_ is null!");
    return worker_->dispatcher();
  }

private:
  struct ListenerUpdateCallbacksNopHandle : public ListenerUpdateCallbacksHandle {};
  Instance& server_;
  ApiListenerPtr api_listener_;

  std::atomic<bool> worker_started_{false};
  std::unique_ptr<ApiListenerWorker> worker_;
};

class ApiListenerManagerFactoryImpl : public ListenerManagerFactory {
public:
  std::unique_ptr<ListenerManager>
  createListenerManager(const Protobuf::Message& config, Instance& server,
                        std::unique_ptr<ListenerComponentFactory>&&, WorkerFactory&, bool,
                        Quic::QuicStatNames&) override {
    const auto& api_config =
        MessageUtil::downcastAndValidate<const envoy::config::bootstrap::v3::ApiListenerManager&>(
            config, server.messageValidationContext().staticValidationVisitor());
    bool use_worker_thread =
        (api_config.threading_model() ==
         envoy::config::bootstrap::v3::ApiListenerManager::STANDALONE_WORKER_THREAD);
    return std::make_unique<ApiListenerManagerImpl>(server, use_worker_thread);
  }
  std::string name() const override { return "envoy.listener_manager_impl.api"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::bootstrap::v3::ApiListenerManager>();
  }
};

DECLARE_FACTORY(ApiListenerManagerFactoryImpl);

} // namespace Server
} // namespace Envoy
