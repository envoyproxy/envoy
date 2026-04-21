#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_session/ai_filter.h"
#include "source/extensions/filters/http/ai_session/ai_session.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_decoder.h"

#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

class AiFilterChain;

// ---------------------------------------------------------------------------
// ActiveAiFilter — per-filter-per-request wrapper
//
// Mirrors ActiveStreamDecoderFilter in filter_manager.h:
//   - Wraps a single AiStreamFilter handle.
//   - Implements AiStreamFilterCallbacks so filters can call back into the chain.
//   - Tracks IterationState for this filter (Continue vs. Stopped).
//   - continueProcessing() delegates to AiFilterChain::resumeFrom(this).
// ---------------------------------------------------------------------------
class ActiveAiFilter : public AiStreamFilterCallbacks,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  ActiveAiFilter(AiFilterChain& chain, std::unique_ptr<AiStreamFilter> filter)
      : chain_(chain), handle_(std::move(filter)) {
    // Inject callbacks — mirrors ActiveStreamDecoderFilter ctor calling
    // handle_->setDecoderFilterCallbacks(*this).
    handle_->setCallbacks(*this);
  }

  // IterationState mirrors ActiveStreamFilterBase::IterationState.
  enum class IterationState : uint8_t {
    Continue, // Filter has not stopped (or has resumed after stopping).
    Stopped,  // Filter returned StopIteration; chain is waiting for continueProcessing().
  };

  // AiStreamFilterCallbacks
  void continueProcessing() override;
  void sendJsonRpcError(int code, absl::string_view message,
                        const nlohmann::json& data) override;
  AiSession& session() override;

  // Called by AiFilterChain to dispatch a specific event to this filter.
  AiFilterStatus callOnJsonRpcBegin();
  AiFilterStatus callOnMethod(absl::string_view method);
  AiFilterStatus callOnId(const nlohmann::json& id);
  AiFilterStatus callOnParams(const nlohmann::json& params);
  AiFilterStatus callOnJsonRpcComplete();
  void callOnError(absl::string_view message);

  bool canIterate() const { return iteration_state_ == IterationState::Continue; }
  bool isStopped() const { return iteration_state_ == IterationState::Stopped; }

  void allowIteration() { iteration_state_ = IterationState::Continue; }
  void stopIteration() { iteration_state_ = IterationState::Stopped; }

private:
  AiFilterChain& chain_;
  std::unique_ptr<AiStreamFilter> handle_;
  IterationState iteration_state_{IterationState::Continue};
};

using ActiveAiFilterPtr = std::unique_ptr<ActiveAiFilter>;

// ---------------------------------------------------------------------------
// AiFilterChain — the FilterManagerImpl analog
//
// One instance per JSON-RPC request. Implements JsonRpcDecoder so it can be
// returned directly from JsonRpcConnectionManagerCallbacks::newStream().
//
// Event dispatch mirrors FilterManagerImpl::decodeHeaders/decodeData:
//
//   onMethod(m)
//     → for each filter[i]:
//         status = filter[i].onMethod(m)
//         if StopIteration: mark stopped at i, return
//     → all filters done: mark method as dispatched
//
//   onId(id) / onParams(p) / onJsonRpcComplete()
//     → if chain is stopped: push to pending_events_ (like buffering body data)
//     → otherwise: iterate from filter[0]
//
//   continueProcessing() called by filter[i]:
//     → allowIteration on filter[i]
//     → resume current event from filter[i+1]
//     → drain pending_events_ through the full chain
//
// This means each filter sees each event exactly once, in document order.
// ---------------------------------------------------------------------------
class AiFilterChain : public JsonRpc::JsonRpcDecoder,
                      public Logger::Loggable<Logger::Id::filter> {
public:
  AiFilterChain(AiSession& session,
                std::vector<std::unique_ptr<AiStreamFilter>> filters);

  // -------------------------------------------------------------------------
  // JsonRpcDecoder — called by JsonRpcParser as the body is parsed
  // -------------------------------------------------------------------------

  void onJsonRpcBegin() override;
  void onMethod(absl::string_view method) override;
  void onId(const nlohmann::json& id) override;
  void onParams(const nlohmann::json& params) override;
  void onJsonRpcComplete() override;
  void onError(absl::string_view message) override;

  // -------------------------------------------------------------------------
  // Called by ActiveAiFilter::continueProcessing()
  // -------------------------------------------------------------------------

  // Resume iteration from the filter AFTER `caller`, then drain any pending
  // events.  Mirrors ActiveStreamDecoderFilter::continueDecoding() calling
  // FilterManagerImpl::continueDecoding().
  void resumeFrom(ActiveAiFilter* caller);

  // Called by ActiveAiFilter::sendJsonRpcError().
  void sendJsonRpcError(int code, absl::string_view message, const nlohmann::json& data);

  // Inject the HTTP filter callbacks so sendJsonRpcError() can call
  // sendLocalReply().  Called by AiSessionManager::newStream() immediately
  // after the chain is created, before any body events arrive.
  void setDecoderCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
    decoder_callbacks_ = &callbacks;
  }

  AiSession& session() { return session_; }

  // Observability accessors (used by tests and outer HTTP filter).
  bool hasError() const { return chain_state_ == ChainState::Error; }
  bool isStopped() const { return chain_state_ == ChainState::Stopped; }
  bool isRunning() const { return chain_state_ == ChainState::Running; }
  size_t pendingEventCount() const { return pending_events_.size(); }

private:
  // -------------------------------------------------------------------------
  // Pending events
  //
  // When the chain is stopped mid-way through an event, subsequent events
  // are queued here in arrival order.  They are drained via drainPending()
  // once the chain resumes — exactly as FilterManager drains buffered body
  // data after continueDecoding().
  // -------------------------------------------------------------------------

  struct MethodEvent   { std::string method; };
  struct IdEvent       { nlohmann::json id; };
  struct ParamsEvent   { nlohmann::json params; };
  struct CompleteEvent {};
  struct ErrorEvent    { std::string message; };

  using PendingEvent = std::variant<MethodEvent, IdEvent, ParamsEvent, CompleteEvent, ErrorEvent>;

  // -------------------------------------------------------------------------
  // Chain state
  // -------------------------------------------------------------------------

  enum class ChainState {
    Running,  // No filter has returned StopIteration; events dispatched immediately.
    Stopped,  // A filter returned StopIteration; new events go to pending_events_.
    Error,    // onError fired; no more events accepted.
  };

  // Dispatch one event through the filter chain starting at `start_idx`.
  // Returns the index of the filter that stopped (or filters_.size() if all
  // completed), and sets chain_state_ accordingly.
  size_t dispatchBegin(size_t start_idx);
  size_t dispatchMethod(absl::string_view method, size_t start_idx);
  size_t dispatchId(const nlohmann::json& id, size_t start_idx);
  size_t dispatchParams(const nlohmann::json& params, size_t start_idx);
  size_t dispatchComplete(size_t start_idx);
  void   dispatchError(absl::string_view message);

  // After resumeFrom(), replay any queued events through the full chain.
  void drainPending();

  // Dispatch a single PendingEvent through the whole chain.
  void replayEvent(const PendingEvent& event);

  AiSession& session_;
  std::vector<ActiveAiFilterPtr> filters_;

  // HTTP filter callbacks injected by setDecoderCallbacks().
  // Used by sendJsonRpcError() to issue sendLocalReply() into the HTTP layer.
  // Null in unit tests that drive the chain directly without an HTTP filter.
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};

  // Index of the filter that last stopped iteration (set when chain_state_ == Stopped).
  // Mirrors FilterManagerImpl tracking which ActiveStreamDecoderFilter stopped.
  size_t stopped_at_{0};

  // The event currently being dispatched when the chain stopped.
  // continueProcessing() resumes this event from stopped_at_+1.
  PendingEvent current_event_;
  bool current_event_valid_{false};

  ChainState chain_state_{ChainState::Running};
  std::vector<PendingEvent> pending_events_;
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
