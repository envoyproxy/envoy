#pragma once

#include <functional>
#include <list>

#include "envoy/init/init.h"

#include "test/mocks/common.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Init {

class MockTarget : public Target {
public:
  MockTarget();
  ~MockTarget();

  MOCK_METHOD1(initialize, void(std::function<void()> callback));

  std::function<void()> callback_;
};

class MockManager : public Manager {
public:
  MockManager();
  ~MockManager();

  void initialize() {
    for (auto target : targets_) {
      target->initialize([this]() -> void { initialized_.ready(); });
    }
  }

  // Init::Manager
  MOCK_METHOD1(registerTarget, void(Target& target));

  std::list<Target*> targets_;
  ReadyWatcher initialized_;
};

} // namespace Init
} // namespace Envoy
