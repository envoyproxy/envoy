#pragma once

#include <cstddef>

#include "envoy/common/pure.h"

namespace Envoy {

class ScopedExecutionContext;
class ExecutionContext {
public:
  ExecutionContext() = default;
  virtual ~ExecutionContext() = default;

  // No copy, move or assign.
  ExecutionContext& operator=(const ExecutionContext&) = delete;
  ExecutionContext& operator=(ExecutionContext&&) = delete;
  ExecutionContext(const ExecutionContext&) = delete;
  ExecutionContext(ExecutionContext&&) = delete;

protected:
  virtual void activate() PURE;
  virtual void deactivate() PURE;

  friend class ScopedExecutionContext;
};

class ScopedExecutionContext {
public:
  ScopedExecutionContext() : ScopedExecutionContext(nullptr) {}
  ScopedExecutionContext(ExecutionContext* context) : context_(context) {
    if (context_ != nullptr) {
      context_->activate();
    }
  }

  // ScopedExecutionContext is move-constructible. No copy or assign.
  ScopedExecutionContext& operator=(const ScopedExecutionContext&) = delete;
  ScopedExecutionContext& operator=(ScopedExecutionContext&&) = delete;
  ScopedExecutionContext(const ScopedExecutionContext&) = delete;
  ScopedExecutionContext(ScopedExecutionContext&& other) {
    if (this == &other) {
      return;
    }
    context_ = other.context_;
    other.context_ = nullptr;
  }

  ~ScopedExecutionContext() {
    if (context_ != nullptr) {
      context_->deactivate();
    }
  }

  // This object is stack-only, it is part of ScopeTrackerScopeState which is
  // also stack-only.
  void* operator new(std::size_t) = delete;

  bool is_null() const { return context_ == nullptr; }

private:
  ExecutionContext* context_;
};

} // namespace Envoy
