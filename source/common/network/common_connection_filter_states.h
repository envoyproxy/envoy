#pragma once

#include "envoy/common/execution_context.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {

static constexpr absl::string_view kConnectionExecutionContextFilterStateName =
    "connection-execution-context";

class ConnectionExecutionContextFilterState
    : public Envoy::StreamInfo::FilterState::Object {
 public:
  explicit ConnectionExecutionContextFilterState(
      std::unique_ptr<ExecutionContext> execution_context)
      : execution_context_(std::move(execution_context)) {}
  
  const ExecutionContext* execution_context() const { return execution_context_.get(); }

 private:
  std::unique_ptr<ExecutionContext> execution_context_;
};

const ExecutionContext* GetConnectionExecutionContextReadOnly(const Network::Connection& connection);

ExecutionContext* GetConnectionExecutionContextMutable(const Network::Connection& connection);

} // namespace Envoy