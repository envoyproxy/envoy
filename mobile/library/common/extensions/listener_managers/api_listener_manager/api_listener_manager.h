#pragma once

#include <memory>

#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"

#include "source/server/listener_manager_factory.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of a lightweight ListenerManager for Envoy Mobile.
 * This does not handle downstream TCP / UDP connections but only the API listener.
 */
class ApiListenerManagerImpl : public ListenerManager, Logger::Loggable<Logger::Id::config> {
public:
  explicit ApiListenerManagerImpl(Instance& server);

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
  absl::Status startWorkers(OptRef<GuardDog>, std::function<void()> callback) override {
    callback();
    return absl::OkStatus();
  }
  void stopListeners(StopListenersType, const Network::ExtraShutdownListenerOptions&) override {}
  void stopWorkers() override {}
  void beginListenerUpdate() override {}
  void endListenerUpdate(FailureStates&&) override {}
  bool isWorkerStarted() override { return true; }
  Http::Context& httpContext() { return server_.httpContext(); }
  ApiListenerOptRef apiListener() override {
    return api_listener_ ? ApiListenerOptRef(std::ref(*api_listener_)) : absl::nullopt;
  }

private:
  Instance& server_;
  ApiListenerPtr api_listener_;
};

class ApiListenerManagerFactoryImpl : public ListenerManagerFactory {
public:
  std::unique_ptr<ListenerManager>
  createListenerManager(Instance& server, std::unique_ptr<ListenerComponentFactory>&&,
                        WorkerFactory&, bool, Quic::QuicStatNames&) override {
    return std::make_unique<ApiListenerManagerImpl>(server);
  }
  std::string name() const override { return "envoy.listener_manager_impl.api"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::listener::v3::ApiListenerManager>();
  }
};

DECLARE_FACTORY(ApiListenerManagerFactoryImpl);

} // namespace Server
} // namespace Envoy
