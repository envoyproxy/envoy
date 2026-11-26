#pragma once

#include <cstdint>
#include <memory>

#include "envoy/buffer/buffer.h"

#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_awaitables.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Injects buffers into the Envoy filter chain via FilterChainBridge, pacing emission against
// filter-chain write watermarks and the per-iteration chunk budget. Cancellation is driven by the
// awaiting coroutine's CoroutineContext; callers must cancel active coroutines before destroying
// ChainWriter or its FilterChainBridge.
class ChainWriter {
public:
  // Maximum bytes emitted per filter-chain injection step.
  static constexpr uint64_t WriteChunkSize = 64 * 1024;

  explicit ChainWriter(FilterChainBridge& bridge)
      : bridge_(bridge), write_resume_cb_(bridge.dispatcher()), budget_cb_(bridge.dispatcher()) {}

  // Moves `data` and emits it into the filter chain in bounded chunks, suspending when the filter
  // chain's write buffer is above its high watermark or the per-iteration chunk budget is spent.
  // Empty buffers complete immediately without suspending.
  Coroutine::Task<absl::Status> inject(Buffer::Instance& data);

private:
  FilterChainBridge& bridge_;
  ReusableSchedulableCallback write_resume_cb_;
  ReusableSchedulableCallback budget_cb_;
};

using ChainWriterPtr = std::unique_ptr<ChainWriter>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
