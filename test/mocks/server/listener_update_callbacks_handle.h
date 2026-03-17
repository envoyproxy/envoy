#pragma once

#include "envoy/server/listener_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
class MockListenerUpdateCallbacksHandle : public ListenerUpdateCallbacksHandle {
public:
  MockListenerUpdateCallbacksHandle();
  ~MockListenerUpdateCallbacksHandle() override;
};
} // namespace Server
} // namespace Envoy
