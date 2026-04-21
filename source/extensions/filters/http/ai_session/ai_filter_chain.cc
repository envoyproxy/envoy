#include "source/extensions/filters/http/ai_session/ai_filter_chain.h"

#include <cassert>

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

// =============================================================================
// ActiveAiFilter
// =============================================================================

void ActiveAiFilter::continueProcessing() {
  ASSERT(isStopped());
  allowIteration();
  // Delegate to the chain, which resumes from the filter *after* this one.
  // Mirrors ActiveStreamDecoderFilter::continueDecoding() delegating to
  // FilterManagerImpl::continueDecoding().
  chain_.resumeFrom(this);
}

void ActiveAiFilter::sendJsonRpcError(int code, absl::string_view message,
                                      const nlohmann::json& data) {
  chain_.sendJsonRpcError(code, message, data);
}

AiSession& ActiveAiFilter::session() { return chain_.session(); }

// Per-event dispatch helpers — thin wrappers that call through to the
// underlying AiStreamFilter and translate the return value.
AiFilterStatus ActiveAiFilter::callOnJsonRpcBegin() { return handle_->onJsonRpcBegin(); }
AiFilterStatus ActiveAiFilter::callOnMethod(absl::string_view method) { return handle_->onMethod(method); }
AiFilterStatus ActiveAiFilter::callOnId(const nlohmann::json& id) { return handle_->onId(id); }
AiFilterStatus ActiveAiFilter::callOnParams(const nlohmann::json& params) { return handle_->onParams(params); }
AiFilterStatus ActiveAiFilter::callOnJsonRpcComplete() { return handle_->onJsonRpcComplete(); }
void ActiveAiFilter::callOnError(absl::string_view message) { handle_->onError(message); }

// =============================================================================
// AiFilterChain — construction
// =============================================================================

AiFilterChain::AiFilterChain(AiSession& session,
                             std::vector<std::unique_ptr<AiStreamFilter>> filters)
    : session_(session) {
  filters_.reserve(filters.size());
  for (auto& f : filters) {
    filters_.emplace_back(std::make_unique<ActiveAiFilter>(*this, std::move(f)));
  }
}

// =============================================================================
// JsonRpcDecoder — public entry points
//
// Each method mirrors how FilterManagerImpl::decodeHeaders / decodeData work:
//   1. If the chain is stopped (a filter previously returned StopIteration),
//      enqueue the event and return immediately — analogous to FilterManager
//      buffering body data when headers have stopped iteration.
//   2. Otherwise, dispatch through the whole chain from filter[0].
//   3. If any filter stops mid-chain, record stopped_at_ and chain_state_.
// =============================================================================

void AiFilterChain::onJsonRpcBegin() {
  if (chain_state_ != ChainState::Running) return;
  dispatchBegin(0);
}

// After any dispatchX() call, a filter may have called sendJsonRpcError()
// which sets chain_state_ = Error.  Guard: never overwrite Error with Stopped.
// This matches how FilterManager handles sendLocalReply() called from within
// a filter callback.
#define HANDLE_STOP_OR_ERROR(stopped_at_var)         \
  do {                                               \
    if (chain_state_ == ChainState::Error) return;   \
    if ((stopped_at_var) < filters_.size()) {        \
      stopped_at_ = (stopped_at_var);                \
      chain_state_ = ChainState::Stopped;            \
    }                                                \
  } while (0)

void AiFilterChain::onMethod(absl::string_view method) {
  if (chain_state_ == ChainState::Error) return;
  if (chain_state_ == ChainState::Stopped) {
    pending_events_.push_back(MethodEvent{std::string(method)});
    return;
  }
  current_event_ = MethodEvent{std::string(method)};
  current_event_valid_ = true;
  const size_t stopped_at = dispatchMethod(method, 0);
  HANDLE_STOP_OR_ERROR(stopped_at);
}

void AiFilterChain::onId(const nlohmann::json& id) {
  if (chain_state_ == ChainState::Error) return;
  if (chain_state_ == ChainState::Stopped) {
    pending_events_.push_back(IdEvent{id});
    return;
  }
  current_event_ = IdEvent{id};
  current_event_valid_ = true;
  const size_t stopped_at = dispatchId(id, 0);
  HANDLE_STOP_OR_ERROR(stopped_at);
}

void AiFilterChain::onParams(const nlohmann::json& params) {
  if (chain_state_ == ChainState::Error) return;
  if (chain_state_ == ChainState::Stopped) {
    pending_events_.push_back(ParamsEvent{params});
    return;
  }
  current_event_ = ParamsEvent{params};
  current_event_valid_ = true;
  const size_t stopped_at = dispatchParams(params, 0);
  HANDLE_STOP_OR_ERROR(stopped_at);
}

void AiFilterChain::onJsonRpcComplete() {
  if (chain_state_ == ChainState::Error) return;
  if (chain_state_ == ChainState::Stopped) {
    pending_events_.push_back(CompleteEvent{});
    return;
  }
  current_event_ = CompleteEvent{};
  current_event_valid_ = true;
  const size_t stopped_at = dispatchComplete(0);
  HANDLE_STOP_OR_ERROR(stopped_at);
}

void AiFilterChain::onError(absl::string_view message) {
  chain_state_ = ChainState::Error;
  dispatchError(message);
}

// =============================================================================
// resumeFrom — called by ActiveAiFilter::continueProcessing()
//
// Mirrors FilterManagerImpl::continueDecoding():
//   1. Find the index of `caller` in the filter list.
//   2. Resume the *current* event (the one that stopped) from caller+1.
//   3. If that event now completes the chain, drain pending_events_ through
//      the full chain (filter[0] to filter[N-1]) for each queued event.
//      This mirrors FilterManager replaying buffered body chunks after
//      continueDecoding() has pushed headers all the way through.
// =============================================================================

void AiFilterChain::resumeFrom(ActiveAiFilter* caller) {
  // Locate the caller's position in the filter list.
  size_t caller_idx = filters_.size();
  for (size_t i = 0; i < filters_.size(); ++i) {
    if (filters_[i].get() == caller) {
      caller_idx = i;
      break;
    }
  }
  ASSERT(caller_idx < filters_.size());

  // Resume from caller+1 with the event that was in progress when the chain stopped.
  const size_t resume_from = caller_idx + 1;
  chain_state_ = ChainState::Running;

  if (current_event_valid_) {
    size_t stopped_at = filters_.size(); // assume completes
    std::visit(
        [&](const auto& event) {
          using T = std::decay_t<decltype(event)>;
          if constexpr (std::is_same_v<T, MethodEvent>) {
            stopped_at = dispatchMethod(event.method, resume_from);
          } else if constexpr (std::is_same_v<T, IdEvent>) {
            stopped_at = dispatchId(event.id, resume_from);
          } else if constexpr (std::is_same_v<T, ParamsEvent>) {
            stopped_at = dispatchParams(event.params, resume_from);
          } else if constexpr (std::is_same_v<T, CompleteEvent>) {
            stopped_at = dispatchComplete(resume_from);
          } else if constexpr (std::is_same_v<T, ErrorEvent>) {
            dispatchError(event.message);
            stopped_at = filters_.size();
          }
        },
        current_event_);

    if (stopped_at < filters_.size()) {
      // Another filter stopped during resume — stay paused at the new position.
      stopped_at_ = stopped_at;
      chain_state_ = ChainState::Stopped;
      return;
    }
    current_event_valid_ = false;
  }

  // Current event finished. Drain queued events through the full chain.
  drainPending();
}

// =============================================================================
// Per-event dispatch helpers
//
// Each iterates filters_[start_idx .. N-1] calling the matching handler.
// Returns the index of the filter that stopped (== filters_.size() if all
// completed without stopping).
//
// Mirrors FilterManagerImpl's per-event iteration loops.
// =============================================================================

size_t AiFilterChain::dispatchBegin(size_t start_idx) {
  for (size_t i = start_idx; i < filters_.size(); ++i) {
    AiFilterStatus status = filters_[i]->callOnJsonRpcBegin();
    if (status == AiFilterStatus::StopIteration) {
      filters_[i]->stopIteration();
      return i;
    }
  }
  return filters_.size();
}

size_t AiFilterChain::dispatchMethod(absl::string_view method, size_t start_idx) {
  for (size_t i = start_idx; i < filters_.size(); ++i) {
    AiFilterStatus status = filters_[i]->callOnMethod(method);
    if (status == AiFilterStatus::StopIteration) {
      filters_[i]->stopIteration();
      return i;
    }
  }
  return filters_.size();
}

size_t AiFilterChain::dispatchId(const nlohmann::json& id, size_t start_idx) {
  for (size_t i = start_idx; i < filters_.size(); ++i) {
    AiFilterStatus status = filters_[i]->callOnId(id);
    if (status == AiFilterStatus::StopIteration) {
      filters_[i]->stopIteration();
      return i;
    }
  }
  return filters_.size();
}

size_t AiFilterChain::dispatchParams(const nlohmann::json& params, size_t start_idx) {
  for (size_t i = start_idx; i < filters_.size(); ++i) {
    AiFilterStatus status = filters_[i]->callOnParams(params);
    if (status == AiFilterStatus::StopIteration) {
      filters_[i]->stopIteration();
      return i;
    }
  }
  return filters_.size();
}

size_t AiFilterChain::dispatchComplete(size_t start_idx) {
  for (size_t i = start_idx; i < filters_.size(); ++i) {
    AiFilterStatus status = filters_[i]->callOnJsonRpcComplete();
    if (status == AiFilterStatus::StopIteration) {
      filters_[i]->stopIteration();
      return i;
    }
  }
  return filters_.size();
}

void AiFilterChain::dispatchError(absl::string_view message) {
  for (auto& f : filters_) {
    f->callOnError(message);
  }
}

// =============================================================================
// drainPending — replay queued events through the full chain
//
// Called after the current event finishes, analogous to how FilterManager
// replays buffered body data after continueDecoding() pushes headers all
// the way through.
// =============================================================================

void AiFilterChain::drainPending() {
  while (!pending_events_.empty() && chain_state_ == ChainState::Running) {
    PendingEvent event = std::move(pending_events_.front());
    pending_events_.erase(pending_events_.begin());

    current_event_ = event;
    current_event_valid_ = true;

    size_t stopped_at = filters_.size();
    std::visit(
        [&](const auto& ev) {
          using T = std::decay_t<decltype(ev)>;
          if constexpr (std::is_same_v<T, MethodEvent>) {
            stopped_at = dispatchMethod(ev.method, 0);
          } else if constexpr (std::is_same_v<T, IdEvent>) {
            stopped_at = dispatchId(ev.id, 0);
          } else if constexpr (std::is_same_v<T, ParamsEvent>) {
            stopped_at = dispatchParams(ev.params, 0);
          } else if constexpr (std::is_same_v<T, CompleteEvent>) {
            stopped_at = dispatchComplete(0);
          } else if constexpr (std::is_same_v<T, ErrorEvent>) {
            chain_state_ = ChainState::Error;
            dispatchError(ev.message);
            stopped_at = filters_.size();
          }
        },
        event);

    if (stopped_at < filters_.size()) {
      stopped_at_ = stopped_at;
      chain_state_ = ChainState::Stopped;
      return; // Remaining pending events stay queued; resumed later.
    }
    current_event_valid_ = false;
  }
}

void AiFilterChain::replayEvent(const PendingEvent& event) {
  // Helper kept for completeness; drainPending() handles the loop directly.
  (void)event;
}

// =============================================================================
// Error response
// =============================================================================

void AiFilterChain::sendJsonRpcError(int code, absl::string_view message,
                                     const nlohmann::json& data) {
  chain_state_ = ChainState::Error;

  nlohmann::json error_response = {
      {"jsonrpc", "2.0"},
      {"id", session_.requestCount()},
      {"error", {{"code", code}, {"message", std::string(message)}, {"data", data}}}};

  const std::string body = error_response.dump();
  ENVOY_LOG(debug, "ai_session: JSON-RPC error: {}", body);

  if (decoder_callbacks_ != nullptr) {
    // Produce a real HTTP response via the filter manager.
    //
    // JSON-RPC 2.0 §5 specifies that errors are returned as HTTP 200 with an
    // error object in the body — the HTTP status code reflects the transport,
    // not the application result.  Use HTTP 400 only for malformed requests
    // that are not valid JSON-RPC at all (those come through onError, not here).
    decoder_callbacks_->sendLocalReply(
        Http::Code::OK, body,
        [](Http::ResponseHeaderMap& headers) {
          headers.setContentType("application/json");
        },
        absl::nullopt, "ai_session_jsonrpc_error");
  }
}

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
