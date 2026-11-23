#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

FilterChainBridge::~FilterChainBridge() {
  detached_ = true;
  write_resume_cb_ = nullptr;
  // Client::onBridgeDetached() unregisters the client, erasing it from registered_clients_.
  // Re-querying begin() on each iteration avoids holding an iterator across flat_hash_set
  // mutations.
  while (!registered_clients_.empty()) {
    (*registered_clients_.begin())->onBridgeDetached();
  }
}

void FilterChainBridge::addUnacked(uint64_t bytes) {
  unacked_ += bytes;
  updateIngestBackpressure();
}

void FilterChainBridge::releaseUnacked(uint64_t bytes) {
  ASSERT(unacked_ >= bytes);
  unacked_ -= bytes;
  updateIngestBackpressure();
}

void FilterChainBridge::updateIngestBackpressure() {
  if (detached_ || high_watermark_ == 0) {
    return; // Detached or ingest flow control disabled.
  }
  if (!source_paused_ && unacked_ > high_watermark_) {
    source_paused_ = true;
    ENVOY_LOG(debug, "ai_protocol_manager: ingest high watermark ({} bytes not durable)", unacked_);
    // Expect at most one synchronous self-echo while pauseSource() is on the stack, then clear any
    // unconsumed expectation on return (e.g. for non-echoing mocks, AsyncStream, or teardown) so a
    // later genuine watermark is never swallowed.
    const uint32_t prev_expected_echoes = expected_high_watermark_echoes_++;
    pauseSource();
    expected_high_watermark_echoes_ =
        std::min(expected_high_watermark_echoes_, prev_expected_echoes);
    // Snapshot registered_clients_ in case a synchronous write completion/error unregisters a
    // client while iterating.
    const std::vector<Client*> clients(registered_clients_.begin(), registered_clients_.end());
    for (Client* client : clients) {
      if (registered_clients_.contains(client)) {
        client->onIngestPaused();
      }
    }
    return;
  }
  if (source_paused_ && unacked_ <= low_watermark_) {
    source_paused_ = false;
    ENVOY_LOG(debug, "ai_protocol_manager: ingest low watermark ({} bytes not durable)", unacked_);
    const uint32_t prev_expected_echoes = expected_low_watermark_echoes_++;
    resumeSource();
    expected_low_watermark_echoes_ = std::min(expected_low_watermark_echoes_, prev_expected_echoes);
  }
}

void FilterChainBridge::detachFromFilterChain() {
  if (!detached_) {
    const bool should_resume_source = source_paused_ && unwindSourcePauseOnDetach();
    source_paused_ = false;
    detached_ = true;
    if (should_resume_source) {
      const uint32_t prev_expected_echoes = expected_low_watermark_echoes_++;
      resumeSource();
      expected_low_watermark_echoes_ =
          std::min(expected_low_watermark_echoes_, prev_expected_echoes);
    }
    unsubscribeWriteWatermarks();
  }
  write_resume_cb_ = nullptr;
  while (!registered_clients_.empty()) {
    (*registered_clients_.begin())->onBridgeDetached();
  }
}

void FilterChainBridge::onAboveWriteWatermark() {
  if (expected_high_watermark_echoes_ > 0) {
    --expected_high_watermark_echoes_;
    return;
  }
  ++write_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: write high watermark (depth={})",
            write_high_watermark_count_);
}

void FilterChainBridge::onBelowWriteWatermark() {
  if (expected_low_watermark_echoes_ > 0) {
    --expected_low_watermark_echoes_;
    return;
  }
  ASSERT(write_high_watermark_count_ > 0);
  --write_high_watermark_count_;
  ENVOY_LOG(debug, "ai_protocol_manager: write low watermark (depth={})",
            write_high_watermark_count_);
  if (write_high_watermark_count_ == 0 && write_resume_cb_ != nullptr) {
    WriteResumeCallback cb = std::exchange(write_resume_cb_, nullptr);
    cb();
  }
}

bool FilterChainBridge::tryConsumeInjectBudget() {
  if (inject_budget_used_ >= InjectChunksPerIteration) {
    return false;
  }
  ++inject_budget_used_;
  return true;
}

void FilterChainBridge::resetInjectBudget() { inject_budget_used_ = 0; }

void DecoderFilterChainBridge::onUnrecoverableError() {
  if (on_error_ != nullptr) {
    ErrorCallback cb = std::exchange(on_error_, nullptr);
    cb();
  }
  stats_.request_external_buffer_error_.inc();
  callbacks_.sendLocalReply(Http::Code::InternalServerError, "AI protocol buffer error", nullptr,
                            std::nullopt, "ai_protocol_manager_external_buffer_error");
}

void EncoderFilterChainBridge::onUnrecoverableError() {
  if (on_error_ != nullptr) {
    ErrorCallback cb = std::exchange(on_error_, nullptr);
    cb();
  }
  stats_.response_external_buffer_error_.inc();
  // On the response path the headers (and possibly some body) may already be in flight.
  // sendLocalReply handles this best-effort: if the response has not started it generates a local
  // reply, otherwise it either ships the reply directly to the downstream codec or resets the
  // stream (see its contract in envoy/http/filter.h). Either way we avoid emitting a truncated
  // payload.
  encoder_callbacks_.sendLocalReply(Http::Code::InternalServerError, "AI protocol buffer error",
                                    nullptr, std::nullopt,
                                    "ai_protocol_manager_external_buffer_error");
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
