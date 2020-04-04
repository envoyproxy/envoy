#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Event {

class DeferredTaskUtil {
private:
  class DeferredTask : public DeferredDeletable {
  public:
    DeferredTask(std::function<void()>&& task) : task_(std::move(task)) {}
    ~DeferredTask() override { task_(); }
    std::function<void()> task_;
  };

public:
  static void deferredRun(Dispatcher& dispatcher, std::function<void()>&& func) {
    dispatcher.deferredDelete(std::make_unique<DeferredTask>(std::move(func)));
  }
};

} // namespace Event
} // namespace Envoy