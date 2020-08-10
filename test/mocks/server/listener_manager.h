#pragma once

#include "envoy/server/listener_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockListenerManager : public ListenerManager {
public:
  MockListenerManager();
  ~MockListenerManager() override;

  MOCK_METHOD(bool, addOrUpdateListener,
              (const envoy::config::listener::v3::Listener& config, const std::string& version_info,
               bool modifiable));
  MOCK_METHOD(void, createLdsApi, (const envoy::config::core::v3::ConfigSource& lds_config));
  MOCK_METHOD(std::vector<std::reference_wrapper<Network::ListenerConfig>>, listeners, ());
  MOCK_METHOD(uint64_t, numConnections, (), (const));
  MOCK_METHOD(bool, removeListener, (const std::string& listener_name));
  MOCK_METHOD(void, startWorkers, (GuardDog & guard_dog));
  MOCK_METHOD(void, stopListeners, (StopListenersType listeners_type));
  MOCK_METHOD(void, stopWorkers, ());
  MOCK_METHOD(void, beginListenerUpdate, ());
  MOCK_METHOD(void, endListenerUpdate, (ListenerManager::FailureStates &&));
  MOCK_METHOD(ApiListenerOptRef, apiListener, ());
};
} // namespace Server
} // namespace Envoy
