#pragma once

#include <cstdint>

#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/chain_writer.h"
#include "source/extensions/filters/http/ai_protocol_manager/payload_reader.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Helper coroutine that reads `[offset, offset + length)` from `source` in bounded chunks and
// injects them into `writer`.
Coroutine::Task<absl::Status> streamPayload(PayloadReader& source, uint64_t offset, uint64_t length,
                                            ChainWriter& writer);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
