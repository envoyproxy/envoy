#include "common/network/upstream_server_name.h"

#include "common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& UpstreamServerName::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.upstream_server_name");
}
} // namespace Network
} // namespace Envoy
