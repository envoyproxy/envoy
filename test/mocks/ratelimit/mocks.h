#pragma once

#include <string>
#include <vector>

#include "envoy/ratelimit/ratelimit.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace RateLimit {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient();

  // RateLimit::Client
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD4(limit, void(RequestCallbacks& callbacks, const std::string& domain,
                           const std::vector<Descriptor>& descriptors, Tracing::Span& parent_span));
};

inline bool operator==(const DescriptorEntry& lhs, const DescriptorEntry& rhs) {
  return lhs.key_ == rhs.key_ && lhs.value_ == rhs.value_;
}

inline bool operator==(const Descriptor& lhs, const Descriptor& rhs) {
  return lhs.entries_ == rhs.entries_;
}

} // namespace RateLimit
} // namespace Envoy
