#pragma once

#include "envoy/common/execution_context.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {

static constexpr absl::string_view kConnectionExecutionContextFilterStateName =
    "connection-execution-context";

// ConnectionExecutionContextFilterState is an optional connection-level filter state goes by the
// name kConnectionExecutionContextFilterStateName. It owns a ExecutionContext, whose
// activate/deactivate methods will be called when a thread starts/finishes running code on behalf
// of the corresponding connection.
class ConnectionExecutionContextFilterState : public Envoy::StreamInfo::FilterState::Object {
public:
  explicit ConnectionExecutionContextFilterState(
      std::unique_ptr<ExecutionContext> execution_context)
      : execution_context_(std::move(execution_context)) {}

  const ExecutionContext* execution_context() const { return execution_context_.get(); }

private:
  std::unique_ptr<ExecutionContext> execution_context_;
};

// Returns the ExecutionContext of a connection, if any. Or nullptr if not found.
const ExecutionContext*
GetConnectionExecutionContextReadOnly(const Network::Connection& connection);

// Returns the mutable ExecutionContext of a connection. Or nullptr if not found.
ExecutionContext* GetConnectionExecutionContextMutable(const Network::Connection& connection);

} // namespace Envoy
