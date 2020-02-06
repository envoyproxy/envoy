#include "extensions/transport_sockets/alts/grpc_tsi.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

void wrapped_tsi_handshaker_destroy(tsi_handshaker* self) {
  if (grpc_core::ExecCtx::Get() == nullptr) {
    grpc_core::ExecCtx exec_ctx;
    tsi_handshaker_destroy(self);
  } else {
    tsi_handshaker_destroy(self);
  }
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy