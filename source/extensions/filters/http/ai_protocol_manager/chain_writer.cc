#include "source/extensions/filters/http/ai_protocol_manager/chain_writer.h"

#include <algorithm>
#include <cstdint>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/status_macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

Coroutine::Task<absl::Status> ChainWriter::inject(Buffer::Instance& data) {
  if (data.length() == 0) {
    co_return absl::OkStatus();
  }

  Buffer::OwnedImpl owned;
  owned.move(data);

  // `inject()` can be entered synchronously from inside `decodeData()` / `encodeData()` (e.g. on an
  // in-memory payload or when `AsyncQueue::tryPush()` wakes a parked response sink), where calling
  // `inject*DataToFilterChain` on-stack trips `Http::FilterManager`'s `FilterCallState` assertion.
  CO_RETURN_IF_ERROR(co_await Coroutine::yield());

  while (owned.length() > 0) {
    CO_RETURN_IF_ERROR(co_await FilterChainWritable(bridge_, write_resume_cb_));
    CO_RETURN_IF_ERROR(co_await InjectionBudgetAvailable(bridge_, budget_cb_));

    const uint64_t to_inject = std::min(WriteChunkSize, static_cast<uint64_t>(owned.length()));
    Buffer::OwnedImpl chunk;
    chunk.move(owned, to_inject);
    CO_RETURN_IF_ERROR(co_await InjectChunk(bridge_, chunk));
  }

  co_return absl::OkStatus();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
