#pragma once

#include <string>
#include <vector>

#include "envoy/ratelimit/ratelimit.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/common/ratelimit/ratelimit.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient() override;

  // RateLimit::Client
  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD(void, limit,
              (RequestCallbacks & callbacks, const std::string& domain,
               const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
               Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info,
               uint32_t hits_addend));
};

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
