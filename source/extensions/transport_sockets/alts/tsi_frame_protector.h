#pragma once

#include "envoy/buffer/buffer.h"

#include "extensions/transport_sockets/alts/grpc_tsi.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

/**
 * A C++ wrapper for tsi_frame_protector interface.
 * For detail of tsi_frame_protector, see
 * https://github.com/grpc/grpc/blob/v1.10.0/src/core/tsi/transport_security_interface.h#L70
 *
 * TODO(lizan): migrate to tsi_zero_copy_grpc_protector for further optimization
 */
class TsiFrameProtector final {
public:
  explicit TsiFrameProtector(CFrameProtectorPtr&& frame_protector);

  /**
   * Wrapper for tsi_frame_protector_protect
   * @param input supplies the input data to protect, the method will drain it when it is processed.
   * @param output supplies the buffer where the protected data will be stored.
   * @return tsi_result the status.
   */
  tsi_result protect(Buffer::Instance& input, Buffer::Instance& output);

  /**
   * Wrapper for tsi_frame_protector_unprotect
   * @param input supplies the input data to unprotect, the method will drain it when it is
   * processed.
   * @param output supplies the buffer where the unprotected data will be stored.
   * @return tsi_result the status.
   */
  tsi_result unprotect(Buffer::Instance& input, Buffer::Instance& output);

private:
  CFrameProtectorPtr frame_protector_;
};

typedef std::unique_ptr<TsiFrameProtector> TsiFrameProtectorPtr;

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
