#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/server/lifecycle_notifier.h"

#include "gmock/gmock.h"
#include "spdlog/spdlog.h"

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
}

}
