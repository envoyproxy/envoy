#include <memory>

#include "server/connection_handler_impl.h"

namespace Envoy {

absl::optional<std::reference_wrapper<ConnectionHandlerImpl::ActiveTcpListener>> a() {
  return nullptr;
}

} // namespace Envoy
