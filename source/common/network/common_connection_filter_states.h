#pragma once

#include "envoy/common/execution_context.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

static constexpr absl::string_view kConnectionExecutionContextFilterStateName =
    "envoy.network.connection_execution_context";

// ConnectionExecutionContextFilterState is an optional connection-level filter state that goes by
// the name kConnectionExecutionContextFilterStateName. It owns a ExecutionContext, whose
// activate/deactivate methods will be called when a thread starts/finishes running code on behalf
// of the corresponding connection.
class ConnectionExecutionContextFilterState : public Envoy::StreamInfo::FilterState::Object {
public:
  // It is safe, although useless, to set execution_context to nullptr.
  explicit ConnectionExecutionContextFilterState(
      std::unique_ptr<ExecutionContext> execution_context)
      : execution_context_(std::move(execution_context)) {}

  ExecutionContext* executionContext() const { return execution_context_.get(); }

private:
  std::unique_ptr<ExecutionContext> execution_context_;
};

// Returns the ExecutionContext of a connection, if any. Or nullptr if not found.
ExecutionContext* getConnectionExecutionContext(const Network::Connection& connection);

} // namespace Network
} // namespace Envoy
