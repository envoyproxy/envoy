#pragma once

#include "envoy/server/listener_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockListenerManager : public ListenerManager {
public:
  MockListenerManager();
  ~MockListenerManager() override;

  MOCK_METHOD(absl::StatusOr<bool>, addOrUpdateListener,
              (const envoy::config::listener::v3::Listener& config, const std::string& version_info,
               bool modifiable),
              (override));
  MOCK_METHOD(void, createLdsApi,
              (const envoy::config::core::v3::ConfigSource& lds_config,
               const xds::core::v3::ResourceLocator*),
              (override));
  MOCK_METHOD(std::vector<std::reference_wrapper<Network::ListenerConfig>>, listeners,
              (ListenerState state), (override));
  MOCK_METHOD(uint64_t, numConnections, (), (const, override));
  MOCK_METHOD(bool, removeListener, (const std::string& listener_name), (override));
  MOCK_METHOD(absl::Status, startWorkers,
              (OptRef<GuardDog> guard_dog, std::function<void()> callback), (override));
  MOCK_METHOD(void, stopListeners,
              (StopListenersType listeners_type,
               const Network::ExtraShutdownListenerOptions& options),
              (override));
  MOCK_METHOD(void, onServerDrainStart,
              (Network::DrainDirection direction, Network::ConnectionDrainEvent drain_event),
              (override));
  MOCK_METHOD(void, stopWorkers, (), (override));
  MOCK_METHOD(void, beginListenerUpdate, (), (override));
  MOCK_METHOD(void, endListenerUpdate, (ListenerManager::FailureStates &&), (override));
  using ListenerManager::apiListener;
  MOCK_METHOD(ApiListenerOptRef, apiListener, (), (override));
  MOCK_METHOD(bool, isWorkerStarted, (), (override));

  ListenerUpdateCallbacksHandlePtr
  addListenerUpdateCallbacks(ListenerUpdateCallbacks& callbacks) override {
    return ListenerUpdateCallbacksHandlePtr{addListenerUpdateCallbacks_(callbacks)};
  }
  MOCK_METHOD(ListenerUpdateCallbacksHandle*, addListenerUpdateCallbacks_,
              (ListenerUpdateCallbacks & callbacks));
};
} // namespace Server
} // namespace Envoy
