#include "extensions/transport_sockets/alts/tsi_frame_protector.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// TODO(lizan): tune size later
static constexpr uint32_t BUFFER_SIZE = 16384;

TsiFrameProtector::TsiFrameProtector(CFrameProtectorPtr&& frame_protector)
    : frame_protector_(std::move(frame_protector)) {}

tsi_result TsiFrameProtector::protect(Buffer::Instance& input, Buffer::Instance& output) {
  ASSERT(frame_protector_);

  unsigned char protected_buffer[BUFFER_SIZE];
  while (input.length() > 0) {
    auto* message_bytes = reinterpret_cast<unsigned char*>(input.linearize(input.length()));
    size_t protected_buffer_size = BUFFER_SIZE;
    size_t processed_message_size = input.length();
    tsi_result result =
        tsi_frame_protector_protect(frame_protector_.get(), message_bytes, &processed_message_size,
                                    protected_buffer, &protected_buffer_size);
    if (result != TSI_OK) {
      ASSERT(result != TSI_INVALID_ARGUMENT && result != TSI_UNIMPLEMENTED);
      return result;
    }
    output.add(protected_buffer, protected_buffer_size);
    input.drain(processed_message_size);
  }

  // TSI may buffer some of the input internally. Flush its buffer to protected_buffer.
  size_t still_pending_size;
  do {
    size_t protected_buffer_size = BUFFER_SIZE;
    tsi_result result = tsi_frame_protector_protect_flush(
        frame_protector_.get(), protected_buffer, &protected_buffer_size, &still_pending_size);
    if (result != TSI_OK) {
      ASSERT(result != TSI_INVALID_ARGUMENT && result != TSI_UNIMPLEMENTED);
      return result;
    }
    output.add(protected_buffer, protected_buffer_size);
  } while (still_pending_size > 0);

  return TSI_OK;
}

tsi_result TsiFrameProtector::unprotect(Buffer::Instance& input, Buffer::Instance& output) {
  ASSERT(frame_protector_);

  unsigned char unprotected_buffer[BUFFER_SIZE];

  while (input.length() > 0) {
    auto* message_bytes = reinterpret_cast<unsigned char*>(input.linearize(input.length()));
    size_t unprotected_buffer_size = BUFFER_SIZE;
    size_t processed_message_size = input.length();
    tsi_result result = tsi_frame_protector_unprotect(frame_protector_.get(), message_bytes,
                                                      &processed_message_size, unprotected_buffer,
                                                      &unprotected_buffer_size);
    if (result != TSI_OK) {
      ASSERT(result != TSI_INVALID_ARGUMENT && result != TSI_UNIMPLEMENTED);
      return result;
    }
    output.add(unprotected_buffer, unprotected_buffer_size);
    input.drain(processed_message_size);
  }

  return TSI_OK;
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
