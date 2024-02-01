#include "source/common/network/common_connection_filter_states.h"

namespace Envoy {
namespace Network {

ExecutionContext* getConnectionExecutionContext(const Network::Connection& connection) {
  const ConnectionExecutionContextFilterState* filter_state =
      connection.streamInfo().filterState().getDataReadOnly<ConnectionExecutionContextFilterState>(
          kConnectionExecutionContextFilterStateName);
  return filter_state == nullptr ? nullptr : filter_state->executionContext();
}

} // namespace Network
} // namespace Envoy
