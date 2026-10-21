#pragma once

#include "envoy/http/early_header_mutation.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockEarlyHeaderMutation : public EarlyHeaderMutation {
public:
  MockEarlyHeaderMutation();

  MOCK_METHOD(bool, mutate, (RequestHeaderMap&, const StreamInfo::StreamInfo&), (const, override));
};

} // namespace Http
} // namespace Envoy
