#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/quic/listener.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class MockQuicListenerCallbacks : public QuicListenerCallbacks {
public:
  MockQuicListenerCallbacks() {}
  ~MockQuicListenerCallbacks() {}

  MOCK_METHOD0(socket, Network::Socket&());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
};

} // namespace Quic
} // namespace Envoy
