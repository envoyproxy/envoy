#pragma once

#include "envoy/network/parent_drained_callback_registrar.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockParentDrainedCallbackRegistrar : public ParentDrainedCallbackRegistrar {
public:
  MOCK_METHOD(void, registerParentDrainedCallback,
              (const Address::InstanceConstSharedPtr& address,
               absl::AnyInvocable<void()> callback));
};

} // namespace Network
} // namespace Envoy
