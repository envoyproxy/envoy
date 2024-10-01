#pragma once

#include <cstddef>

#include "envoy/common/pure.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/non_copyable.h"

namespace Envoy {

#ifdef ENVOY_ENABLE_EXECUTION_CONTEXT

static constexpr absl::string_view kConnectionExecutionContextFilterStateName =
    "envoy.network.connection_execution_context";

class ScopedExecutionContext;

// ExecutionContext can be inherited by subclasses to represent arbitrary information associated
// with the execution of a piece of code. activate/deactivate are called when the said execution
// starts/ends. For an example usage, please see
// https://github.com/envoyproxy/envoy/issues/32012.
class ExecutionContext : public StreamInfo::FilterState::Object, NonCopyable {
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
  ScopedExecutionContext(const ScopeTrackedObject* object)
      : context_(object != nullptr ? getExecutionContext(object->trackedStream()) : nullptr) {
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
  ExecutionContext* getExecutionContext(OptRef<const StreamInfo::StreamInfo> info) {
    if (!info.has_value()) {
      return nullptr;
    }
    const auto* const_context = info->filterState().getDataReadOnly<ExecutionContext>(
        kConnectionExecutionContextFilterStateName);
    return const_cast<ExecutionContext*>(const_context);
  }

  ExecutionContext* context_;
};

#endif

} // namespace Envoy
