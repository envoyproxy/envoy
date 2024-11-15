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

  MOCK_METHOD(bool, canCreate, ());
  MOCK_METHOD(void, inc, ());
  MOCK_METHOD(void, dec, ());
  MOCK_METHOD(void, decBy, (uint64_t));
  MOCK_METHOD(uint64_t, max, ());
  MOCK_METHOD(uint64_t, count, (), (const));
};
} // namespace Upstream
} // namespace Envoy
