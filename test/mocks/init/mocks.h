#pragma once

#include "envoy/init/init.h"

#include "gmock/gmock.h"
#include "init/manager_impl.h"

namespace Envoy {
namespace Init {

class MockManager : public Manager {
public:
  MOCK_CONST_METHOD0(state, Manager::State());
  MOCK_METHOD1(add, void(const TargetReceiver&));
  MOCK_METHOD1(initialize, void(const Receiver&));
};

} // namespace Init
} // namespace Envoy
