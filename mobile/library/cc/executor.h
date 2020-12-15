#pragma once

// NOLINT(namespace-envoy)

#include <functional>
#include <memory>

class Executor {
public:
  virtual ~Executor();

  virtual void execute(std::function<void()> closure) = 0;
};

using ExecutorSharedPtr = std::shared_ptr<Executor>;
