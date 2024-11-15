#include "source/common/network/upstream_socket_options_filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& UpstreamSocketOptionsFilterState::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.upstream_socket_options");
}

} // namespace Network
} // namespace Envoy
