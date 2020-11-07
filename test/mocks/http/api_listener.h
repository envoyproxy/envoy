#pragma once
#include "envoy/http/api_listener.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockApiListener : public ApiListener {
public:
  MockApiListener();
  ~MockApiListener() override;

  // Http::ApiListener
  MOCK_METHOD(RequestDecoder&, newStream,
              (ResponseEncoder & response_encoder, bool is_internally_created));
};

} // namespace Http
} // namespace Envoy
