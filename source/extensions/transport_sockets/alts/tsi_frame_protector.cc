#include "extensions/transport_sockets/alts/tsi_frame_protector.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"

#include "grpc/slice_buffer.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

TsiFrameProtector::TsiFrameProtector(CFrameProtectorPtr&& frame_protector)
    : frame_protector_(std::move(frame_protector)) {}

tsi_result TsiFrameProtector::protect(Buffer::Instance& input, Buffer::Instance& output) {
  ASSERT(frame_protector_);

  if (input.length() == 0) {
    return TSI_OK;
  }

  grpc_core::ExecCtx exec_ctx;
  grpc_slice input_slice = grpc_slice_from_copied_buffer(
      reinterpret_cast<char*>(input.linearize(input.length())), input.length());

  grpc_slice_buffer message_buffer;
  grpc_slice_buffer_init(&message_buffer);
  grpc_slice_buffer_add(&message_buffer, input_slice);

  grpc_slice_buffer protected_buffer;
  grpc_slice_buffer_init(&protected_buffer);

  tsi_result result = tsi_zero_copy_grpc_protector_protect(frame_protector_.get(), &message_buffer,
                                                           &protected_buffer);

  if (result != TSI_OK) {
    ASSERT(result != TSI_INVALID_ARGUMENT && result != TSI_UNIMPLEMENTED);
    grpc_slice_buffer_destroy(&message_buffer);
    grpc_slice_buffer_destroy(&protected_buffer);
    return result;
  }

  const size_t protected_data_length = protected_buffer.length;
  char* protected_data = new char[protected_data_length];

  grpc_slice_buffer_move_first_into_buffer(&protected_buffer, protected_data_length,
                                           protected_data);

  auto fragment = new Buffer::BufferFragmentImpl(
      protected_data, protected_data_length,
      [protected_data](const void*, size_t,
                       const Envoy::Buffer::BufferFragmentImpl* this_fragment) {
        delete[] protected_data;
        delete this_fragment;
      });

  output.addBufferFragment(*fragment);
  input.drain(input.length());

  grpc_slice_buffer_destroy(&message_buffer);
  grpc_slice_buffer_destroy(&protected_buffer);

  return TSI_OK;
}

tsi_result TsiFrameProtector::unprotect(Buffer::Instance& input, Buffer::Instance& output) {
  ASSERT(frame_protector_);

  if (input.length() == 0) {
    return TSI_OK;
  }

  grpc_core::ExecCtx exec_ctx;
  grpc_slice input_slice = grpc_slice_from_copied_buffer(
      reinterpret_cast<char*>(input.linearize(input.length())), input.length());

  grpc_slice_buffer protected_buffer;
  grpc_slice_buffer_init(&protected_buffer);
  grpc_slice_buffer_add(&protected_buffer, input_slice);

  grpc_slice_buffer unprotected_buffer;
  grpc_slice_buffer_init(&unprotected_buffer);

  tsi_result result = tsi_zero_copy_grpc_protector_unprotect(
      frame_protector_.get(), &protected_buffer, &unprotected_buffer);

  if (result != TSI_OK) {
    ASSERT(result != TSI_INVALID_ARGUMENT && result != TSI_UNIMPLEMENTED);
    grpc_slice_buffer_destroy(&protected_buffer);
    grpc_slice_buffer_destroy(&unprotected_buffer);
    return result;
  }

  const size_t unprotected_data_length = unprotected_buffer.length;
  char* unprotected_data = new char[unprotected_data_length];

  grpc_slice_buffer_move_first_into_buffer(&unprotected_buffer, unprotected_data_length,
                                           unprotected_data);

  auto fragment = new Buffer::BufferFragmentImpl(
      unprotected_data, unprotected_data_length,
      [unprotected_data](const void*, size_t,
                         const Envoy::Buffer::BufferFragmentImpl* this_fragment) {
        delete[] unprotected_data;
        delete this_fragment;
      });

  output.addBufferFragment(*fragment);
  input.drain(input.length());

  grpc_slice_buffer_destroy(&protected_buffer);
  grpc_slice_buffer_destroy(&unprotected_buffer);

  return TSI_OK;
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
