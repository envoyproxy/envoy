#pragma once

// Some gRPC headers contains old style cast and unused parameter which doesn't
// compile with -Werror, ignoring those compiler warning since we don't have
// control on those source codes. This works with GCC and Clang.

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include "grpc/grpc_security.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/alts/handshaker/transport_security_common_api.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "source/common/common/c_smart_ptr.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

using CFrameProtectorPtr =
    CSmartPtr<tsi_zero_copy_grpc_protector, tsi_zero_copy_grpc_protector_destroy>;

using CHandshakerResultPtr = CSmartPtr<tsi_handshaker_result, tsi_handshaker_result_destroy>;
using CHandshakerPtr = CSmartPtr<tsi_handshaker, tsi_handshaker_destroy>;

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
