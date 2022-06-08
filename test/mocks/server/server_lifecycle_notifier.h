#pragma once

#include "envoy/server/lifecycle_notifier.h"

#include "spdlog/spdlog.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockServerLifecycleNotifier : public ServerLifecycleNotifier {
public:
  MockServerLifecycleNotifier();
  ~MockServerLifecycleNotifier() override;

  MOCK_METHOD(ServerLifecycleNotifier::HandlePtr, registerCallback, (Stage, StageCallback));
  MOCK_METHOD(ServerLifecycleNotifier::HandlePtr, registerCallback,
              (Stage, StageCallbackWithCompletion));
};
} // namespace Server
} // namespace Envoy
