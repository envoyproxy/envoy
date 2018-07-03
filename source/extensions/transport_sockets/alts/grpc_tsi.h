#pragma once

// Some gRPC headers contains old style cast and unused parameter which doesn't
// compile with -Werror, ignoring those compiler warning since we don't have
// control on those source codes. This works with GCC and Clang.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wold-style-cast"

#include "grpc/grpc_security.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security_interface.h"

#pragma GCC diagnostic pop

#include "common/common/c_smart_ptr.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

typedef CSmartPtr<tsi_frame_protector, tsi_frame_protector_destroy> CFrameProtectorPtr;

typedef CSmartPtr<tsi_handshaker_result, tsi_handshaker_result_destroy> CHandshakerResultPtr;
typedef CSmartPtr<tsi_handshaker, tsi_handshaker_destroy> CHandshakerPtr;

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
