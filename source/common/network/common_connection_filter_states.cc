#include "source/common/network/common_connection_filter_states.h"

namespace Envoy {
namespace Network {

const ExecutionContext*
GetConnectionExecutionContextReadOnly(const Network::Connection& connection) {
  const ConnectionExecutionContextFilterState* filter_state =
      connection.streamInfo().filterState().getDataReadOnly<ConnectionExecutionContextFilterState>(
          kConnectionExecutionContextFilterStateName);
  return filter_state == nullptr ? nullptr : filter_state->execution_context();
}

ExecutionContext* GetConnectionExecutionContextMutable(const Network::Connection& connection) {
  return const_cast<ExecutionContext*>(GetConnectionExecutionContextReadOnly(connection));
}

} // namespace Network
} // namespace Envoy
