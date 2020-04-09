#pragma once

#include "envoy/http/codec.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockStream : public Stream {
public:
  MockStream();
  ~MockStream() override;

  // Http::Stream
  MOCK_METHOD(void, addCallbacks, (StreamCallbacks & callbacks));
  MOCK_METHOD(void, removeCallbacks, (StreamCallbacks & callbacks));
  MOCK_METHOD(void, resetStream, (StreamResetReason reason));
  MOCK_METHOD(void, readDisable, (bool disable));
  MOCK_METHOD(void, setWriteBufferWatermarks, (uint32_t, uint32_t));
  MOCK_METHOD(uint32_t, bufferLimit, ());
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, connectionLocalAddress, ());

  std::list<StreamCallbacks*> callbacks_{};
  Network::Address::InstanceConstSharedPtr connection_local_address_;

  void runHighWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onAboveWriteBufferHighWatermark();
    }
  }

  void runLowWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onBelowWriteBufferLowWatermark();
    }
  }
};

} // namespace Http
} // namespace Envoy
