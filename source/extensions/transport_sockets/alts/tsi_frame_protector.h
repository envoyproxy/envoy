#pragma once

#include "envoy/buffer/buffer.h"

#include "source/extensions/transport_sockets/alts/grpc_tsi.h"

#include "grpc/slice_buffer.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

/**
 * A C++ wrapper for tsi_frame_protector interface.
 * For detail of tsi_frame_protector, see
 * https://github.com/grpc/grpc/blob/v1.10.0/src/core/tsi/transport_security_interface.h#L70
 */
class TsiFrameProtector final {
public:
  explicit TsiFrameProtector(CFrameProtectorPtr&& frame_protector);

  /**
   * Wrapper for tsi_frame_protector_protect
   * @param input_slice supplies the input data to protect. Its ownership will
   * be transferred.
   * @param output supplies the buffer where the protected data will be stored.
   * @return tsi_result the status.
   */
  tsi_result protect(const grpc_slice& input_slice, Buffer::Instance& output);

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

using TsiFrameProtectorPtr = std::unique_ptr<TsiFrameProtector>;

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
