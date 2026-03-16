#pragma once

#include <string>

#include "envoy/server/listener_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
class MockListenerUpdateCallbacks : public ListenerUpdateCallbacks {
public:
  MockListenerUpdateCallbacks();
  ~MockListenerUpdateCallbacks() override;

  MOCK_METHOD(void, onListenerAddOrUpdate,
              (absl::string_view listener_name, const Network::ListenerConfig& listener_config));
  MOCK_METHOD(void, onListenerRemoval, (const std::string& listener_name));
};
} // namespace Server
} // namespace Envoy
