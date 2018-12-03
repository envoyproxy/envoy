#pragma once

#include <string>
#include <vector>

#include "envoy/ratelimit/ratelimit.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient();

  // RateLimit::Client
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD4(limit, void(RequestCallbacks& callbacks, const std::string& domain,
                           const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                           Tracing::Span& parent_span));
};

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
