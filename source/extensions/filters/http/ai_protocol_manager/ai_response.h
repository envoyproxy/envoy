#pragma once

#include <optional>

#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/sse/sse_event.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Delivers the next SSE frame to a filter; nullopt marks the end of the response stream. Called
// repeatedly, once per frame, for the lifetime of the stream.
using SseStreamReceiver =
    absl::AnyInvocable<Coroutine::Task<absl::StatusOr<std::optional<SseEventPtr>>>()>;

// Forwards one SSE frame to the next filter in the chain. The chain rejects a null frame.
using SseStreamPropagator = absl::AnyInvocable<Coroutine::Task<absl::Status>(SseEventPtr)>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
