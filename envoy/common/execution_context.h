#pragma once

#include <cstddef>

#include "envoy/common/pure.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/macros.h"
#include "source/common/common/non_copyable.h"

namespace Envoy {

#ifdef ENVOY_ENABLE_EXECUTION_CONTEXT

static constexpr absl::string_view kConnectionExecutionContextFilterStateName =
    "envoy.network.connection_execution_context";

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
class ExecutionContext : public StreamInfo::FilterState::Object, NonCopyable {
public:
  static void setEnabled(bool value) { enabled().store(value, std::memory_order_relaxed); }

  static bool isEnabled() { return enabled().load(std::memory_order_relaxed); }

  static ExecutionContext* fromStreamInfo(OptRef<const StreamInfo::StreamInfo> info) {
    if (!isEnabled() || !info.has_value()) {
      return nullptr;
    }
    const auto* const_context = info->filterState().getDataReadOnly<ExecutionContext>(
        kConnectionExecutionContextFilterStateName);
    return const_cast<ExecutionContext*>(const_context);
  }

  // Called when enters a scope in which |*span| is active.
  // Returns an object that can do some cleanup when exits the scope.
  virtual Envoy::Cleanup onScopeEnter(Envoy::Tracing::Span* span) PURE;
  // Called when enters a scope in which |*filter_context| is active.
  // Returns an object that can do some cleanup when exits the scope.
  virtual Envoy::Cleanup onScopeEnter(const Http::FilterContext* filter_context) PURE;

protected:
  // Called when the current thread starts to run code on behalf of the owner of this object.
  // protected because it should only be called by ScopedExecutionContext.
  virtual void activate() PURE;
  // Called when the current thread stops running code on behalf of the owner of this object.
  // protected because it should only be called by ScopedExecutionContext.
  virtual void deactivate() PURE;

private:
  static std::atomic<bool>& enabled() { MUTABLE_CONSTRUCT_ON_FIRST_USE(std::atomic<bool>); }

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
  ScopedExecutionContext(const ScopeTrackedObject* object)
      : context_(object != nullptr ? ExecutionContext::fromStreamInfo(object->trackedStream())
                                   : nullptr) {
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

#define ENVOY_EXECUTION_SCOPE_CAT_(a, b) a##b
#define ENVOY_EXECUTION_SCOPE_CAT(a, b) ENVOY_EXECUTION_SCOPE_CAT_(a, b)
// Invoked when |scopedObject| is active from the current line to the end of the current c++ scope.
// |trackedStream| is a OptRef<const StreamInfo> from which a ExecutionContext is extracted.
// |scopedObject| is a pointer to a Envoy::Tracing::Span or a Http::FilterContext.
#define ENVOY_EXECUTION_SCOPE(trackedStream, scopedObject)                                         \
  Envoy::Cleanup ENVOY_EXECUTION_SCOPE_CAT(on_scope_exit_, __LINE__) =                             \
      [execution_context = ExecutionContext::fromStreamInfo(trackedStream),                        \
       scoped_object = (scopedObject)] {                                                           \
        if (execution_context == nullptr) {                                                        \
          return Envoy::Cleanup::Noop();                                                           \
        }                                                                                          \
        return execution_context->onScopeEnter(scoped_object);                                     \
      }()
#else
#define ENVOY_EXECUTION_SCOPE(trackedStream, scopedObject)
#endif

} // namespace Envoy
