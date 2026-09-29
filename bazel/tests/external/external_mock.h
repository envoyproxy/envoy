#pragma once

#include "gmock/gmock.h"

namespace External {

class MockExternalValueProvider {
public:
  MOCK_METHOD(int, value, (), (const));
};

} // namespace External
