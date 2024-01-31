#pragma once

#include <cstddef>

#include "envoy/common/pure.h"

#include "source/common/common/non_copyable.h"

namespace Envoy {

class ScopedExecutionContext;

// ExecutionContext can be inherited by subclasses to represent arbitrary information associated
// with the execution of a piece of code. activate/deactivate are called when the said execution
// starts/ends. For an example usage, please see
// https://github.com/envoyproxy/envoy/issues/32012.
class ExecutionContext : NonCopyable {
public:
  ExecutionContext() = default;
  virtual ~ExecutionContext() = default;

protected:
  // Called when the current thread starts to run code on behalf of the owner of this object.
  // protected because it should only be called by ScopedExecutionContext.
  virtual void activate() PURE;
  // Called when the current thread stops running code on behalf of the owner of this object.
  // protected because it should only be called by ScopedExecutionContext.
  virtual void deactivate() PURE;

  friend class ScopedExecutionContext;
};

// ScopedExecutionContext is a stack-only RAII object to call ExecutionContext::activate on
// construction and ExecutionContext::deactivate on destruction.
//
// ScopedExecutionContext is intened to be used in a simple c++ scope:
//   {
//     ExecutionContext context;
//     // context.activate() called here.
//     ScopedExecutionContext scoped_execution_context(&context);
//     // context.deactivate() called when scoped_execution_context destructs.
//   }
class ScopedExecutionContext : NonCopyable {
public:
  ScopedExecutionContext() : ScopedExecutionContext(nullptr) {}
  ScopedExecutionContext(ExecutionContext* context) : context_(context) {
    if (context_ != nullptr) {
      context_->activate();
    }
  }

  ~ScopedExecutionContext() {
    if (context_ != nullptr) {
      context_->deactivate();
    }
  }

  // This object is stack-only, it is part of ScopeTrackerScopeState which is
  // also stack-only.
  void* operator new(std::size_t) = delete;

  bool isNull() const { return context_ == nullptr; }

private:
  ExecutionContext* context_;
};

} // namespace Envoy
