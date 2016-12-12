#pragma once

#include <gmock/gmock-generated-nice-strict.h>
#include "envoy/ratelimit/ratelimit.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

namespace RateLimit {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient();

  // RateLimit::Client
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD4(limit,
               void(RequestCallbacks& callbacks, const std::string& domain,
                    const std::vector<Descriptor>& descriptors, const std::string& request_id));
};

inline bool operator==(const DescriptorEntry& lhs, const DescriptorEntry& rhs) {
  return lhs.key_ == rhs.key_ && lhs.value_ == rhs.value_;
}

inline bool operator==(const Descriptor& lhs, const Descriptor& rhs) {
  return lhs.entries_ == rhs.entries_;
}

class MockFilterConfig : public FilterConfig {
public:
  MockFilterConfig();
  ~MockFilterConfig();

  // RateLimit::FilterConfig
  MOCK_CONST_METHOD0(domain, const std::string&());
  MOCK_CONST_METHOD0(localServiceCluster, const std::string&());
  MOCK_CONST_METHOD0(stage, int64_t());
  MOCK_METHOD0(runtime, Runtime::Loader&());
  MOCK_METHOD0(stats, Stats::Store&());

  std::string domain_;
  std::string local_service_cluster_;
  int64_t stage_{};
  testing::NiceMock<Runtime::MockLoader> loader_;
  testing::NiceMock<Stats::MockStore> store_;
};
} // RateLimit
