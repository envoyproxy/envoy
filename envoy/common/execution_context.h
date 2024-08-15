#pragma once

#include <atomic>
#include <cstddef>
#include <utility>

#include "envoy/common/pure.h"

#include "source/common/common/non_copyable.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {

namespace Internal {
inline std::atomic<bool>& execution_context_enabled() {
  static std::atomic<bool> enabled = false;
  return enabled;
}
} // namespace Internal

inline void enableExecutionContext() {
  Internal::execution_context_enabled().store(true, std::memory_order_relaxed);
}

inline bool isExecutionContextEnabled() {
  return Internal::execution_context_enabled().load(std::memory_order_relaxed);
}

namespace Http {
struct FilterContext;
}

namespace Tracing {
class Span;
}

class ScopedExecutionContext;

// ExecutionScope is a stack-only RAII object whose lifetime is tied to a c++ scope of interest.
// One such example is a "scope for an active stream filter" in FilterManager:
//
// for (ActiveStreamEncoderFilterPtr &filter : encoder_filters_) {
//   auto filter_scope = scopeForFilter(executionContext(), filter->filter_context_);
//   ... Use |filter| here ...
// }
//
// User of this class is expected to do something at the start and/or end of the
// scope, the end-of-scope callback is provided via a constructor argument.
class ExecutionScope : NonCopyable {
public:
  ExecutionScope() = default;
  explicit ExecutionScope(absl::AnyInvocable<void() &&> exit_cb) : exit_cb_(std::move(exit_cb)) {}

  ~ExecutionScope() {
    if (exit_cb_ != nullptr) {
      std::move(exit_cb_)();
    }
  }

  bool hasExitCallback() const { return exit_cb_ != nullptr; }

  // This object is stack-only.
  void* operator new(std::size_t) = delete;

private:
  absl::AnyInvocable<void() &&> exit_cb_;
};

// ExecutionContext can be inherited by subclasses to represent arbitrary information associated
// with the execution of a piece of code. activate/deactivate are called when the said execution
// starts/ends. For an example usage, please see
// https://github.com/envoyproxy/envoy/issues/32012.
class ExecutionContext : NonCopyable {
public:
  ExecutionContext() = default;
  virtual ~ExecutionContext() = default;

  static ExecutionScope makeScopeForSpan(ExecutionContext* context, Tracing::Span* span) {
    if (context == nullptr || span == nullptr) {
      return ExecutionScope();
    }
    return context->scopeForSpan(*span);
  }

  static ExecutionScope makeScopeForFilter(ExecutionContext* context,
                                           const Http::FilterContext& filter_context) {
    if (context == nullptr) {
      return ExecutionScope();
    }
    return context->scopeForFilter(filter_context);
  }

protected:
  // Called when the current thread starts to run code on behalf of the owner of this object.
  // protected because it should only be called by ScopedExecutionContext.
  virtual void activate() PURE;
  // Called when the current thread stops running code on behalf of the owner of this object.
  // protected because it should only be called by ScopedExecutionContext.
  virtual void deactivate() PURE;

  virtual ExecutionScope scopeForSpan(Envoy::Tracing::Span& span) PURE;
  virtual ExecutionScope scopeForFilter(const Http::FilterContext& filter_context) PURE;

  friend class ScopedExecutionContext;
};

// ScopedExecutionContext is a stack-only RAII object to call ExecutionContext::activate on
// construction and ExecutionContext::deactivate on destruction.
//
// ScopedExecutionContext is intended to be used in a simple c++ scope:
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
