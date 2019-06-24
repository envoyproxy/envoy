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
  MOCK_METHOD1(addCallbacks, void(StreamCallbacks& callbacks));
  MOCK_METHOD1(removeCallbacks, void(StreamCallbacks& callbacks));
  MOCK_METHOD1(resetStream, void(StreamResetReason reason));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD2(setWriteBufferWatermarks, void(uint32_t, uint32_t));
  MOCK_METHOD0(bufferLimit, uint32_t());

  std::list<StreamCallbacks*> callbacks_{};

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
