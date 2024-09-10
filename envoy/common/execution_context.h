#pragma once

#include <cstddef>

#include "envoy/common/pure.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/non_copyable.h"

namespace Envoy {

namespace Http {
struct FilterContext;
}

namespace Tracing {
class Span;
}

class ScopedExecutionContext;

// ExecutionContext can be inherited by subclasses to represent arbitrary information associated
// with the execution of a piece of code. activate/deactivate are called when the said execution
// starts/ends. For an example usage, please see
// https://github.com/envoyproxy/envoy/issues/32012.
class ExecutionContext : NonCopyable {
public:
  ExecutionContext() = default;
  virtual ~ExecutionContext() = default;

  // Called when enters a scope in which |span| is active.
  // Returns an object that can do some cleanup when exits the scope.
  virtual Envoy::Cleanup onScopeEnter(Envoy::Tracing::Span& span) PURE;
  // Called when enters a scope in which |filter_context| is active.
  // Returns an object that can do some cleanup when exits the scope.
  virtual Envoy::Cleanup onScopeEnter(const Http::FilterContext& filter_context) PURE;

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

#ifdef ENVOY_ENABLE_EXECUTION_SCOPE
#define ENVOY_EXECUTION_SCOPE_CAT_(a, b) a##b
#define ENVOY_EXECUTION_SCOPE_CAT(a, b) ENVOY_EXECUTION_SCOPE_CAT_(a, b)
// Invoked when |scopedObject| is active from the current line to the end of the current c++ scope.
// |executionContext| is a pointer to the current ExecutionContext.
// |scopedObject| is a pointer to a Envoy::Tracing::Span or a Http::FilterContext.
#define ENVOY_EXECUTION_SCOPE(executionContext, scopedObject)                                      \
  Envoy::Cleanup ENVOY_EXECUTION_SCOPE_CAT(on_scope_exit_, __LINE__) =                             \
      [execution_context = (executionContext), scoped_object = (scopedObject)] {                   \
        if (execution_context == nullptr || scoped_object == nullptr) {                            \
          return Envoy::Cleanup::Noop();                                                           \
        }                                                                                          \
        return execution_context->onScopeEnter(*scoped_object);                                    \
      }()
#else
#define ENVOY_EXECUTION_SCOPE(executionContext, scopedObject)
#endif

} // namespace Envoy
