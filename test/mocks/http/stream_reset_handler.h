#pragma once

#include "envoy/http/stream_reset_handler.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockStreamResetHandler : public StreamResetHandler {
public:
  MockStreamResetHandler() = default;

  // Http::StreamResetHandler
  MOCK_METHOD(void, resetStream, (StreamResetReason reason));
};

} // namespace Http
} // namespace Envoy
