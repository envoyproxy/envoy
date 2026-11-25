#include "source/extensions/filters/http/ai_protocol_manager/stream_payload.h"

#include <algorithm>
#include <cstdint>

#include "envoy/buffer/buffer.h"

#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/chain_writer.h"
#include "source/extensions/filters/http/ai_protocol_manager/payload_reader.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

Coroutine::Task<absl::Status> streamPayload(PayloadReader& source, uint64_t offset, uint64_t length,
                                            ChainWriter& writer) {
  PayloadReaderSharedPtr reader = source.shared_from_this();
  if (offset > reader->length() || length > reader->length() - offset) {
    co_return absl::InvalidArgumentError("payload stream range out of bounds");
  }

  uint64_t current = offset;
  const uint64_t end = offset + length;
  while (current < end) {
    const uint64_t chunk_len = std::min(ChainWriter::WriteChunkSize, end - current);
    ASSIGN_OR_CO_RETURN(Buffer::InstancePtr chunk, co_await reader->readChunk(current, chunk_len));
    if (chunk == nullptr || chunk->length() == 0) {
      co_return absl::DataLossError("external buffer returned empty chunk");
    }
    current += chunk->length();
    CO_RETURN_IF_ERROR(co_await writer.inject(*chunk));
  }

  co_return absl::OkStatus();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
