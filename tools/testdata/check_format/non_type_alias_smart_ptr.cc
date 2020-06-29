#include <memory>

#include "envoy/network/connection.h"

namespace Envoy {

std::unique_ptr<Network::Connection> a() { return nullptr; }

} // namespace Envoy