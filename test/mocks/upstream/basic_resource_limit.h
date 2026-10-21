#pragma once

#include "envoy/common/resource.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockBasicResourceLimit : public ResourceLimit {
public:
  MockBasicResourceLimit();
  ~MockBasicResourceLimit() override;

  MOCK_METHOD(bool, canCreate, (), (override));
  MOCK_METHOD(void, inc, (), (override));
  MOCK_METHOD(void, dec, (), (override));
  MOCK_METHOD(void, decBy, (uint64_t), (override));
  MOCK_METHOD(uint64_t, max, (), (override));
  MOCK_METHOD(uint64_t, count, (), (const, override));
};
} // namespace Upstream
} // namespace Envoy
