#pragma once

#include <memory>

#include "envoy/stream_info/filter_state.h"

#include "source/common/stream_info/filter_state_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * UpstreamFilterState provides a per-attempt FilterState overlay for upstream HTTP filters.
 *
 * - On UpstreamFilterState (streamInfo().filterState()):
 *   - Reads and mutable lookups check local_filter_state_ (attempt-local FilterChain -> shared
 *     upstream Request) first and then fall back to downstream_filter_state_, without reading
 *     upstream_conn_filter_state_.
 *   - Writes at LifeSpan::FilterChain stage in local_filter_state_ so failed retry or hedged
 *     attempts discard FilterChain writes. Once committed_, new FilterChain writes also write
 *     through immediately to downstream_filter_state_.
 *   - Writes at LifeSpan::Request write through immediately to downstream_filter_state_ at
 *     LifeSpan::Request and record in shared_request_lifespan_filter_state
 *     (local_filter_state_.parent()), sharing Request-lifetime state across upstream attempts and
 *     exposing it in %UPSTREAM_FILTER_STATE%.
 *   - Writes at >= LifeSpan::Connection trigger IS_ENVOY_BUG("FilterStateAccessViolation...") if
 *     the key already exists in local_filter_state_; otherwise they write directly to
 *     downstream_filter_state_ at LifeSpan::Connection, matching UpstreamRequest::connection() and
 *     avoiding any mid-request flip when onPoolReady() binds the backend connection.
 * - On upstreamOnlyFilterState() (UpstreamInfo::upstreamFilterState(), %UPSTREAM_FILTER_STATE%):
 *   - Thin view over (local_filter_state_, upstream_conn_filter_state_) with no fallback to
 *     downstream_filter_state_.
 *   - Writes at >= LifeSpan::Connection target upstream_conn_filter_state_ (lazily buffered in a
 *     FilterStateImpl(LifeSpan::Connection) prior to bindUpstreamConnectionFilterState() and merged
 *     into the bound backend connection FilterState).
 *   - Writes at LifeSpan::FilterChain or LifeSpan::Request delegate to UpstreamFilterState.
 * - commitToDownstream() merges winning attempt-local FilterChain entries into
 *   downstream_filter_state_ and enables write-through for post-commit FilterChain writes.
 */
class UpstreamFilterState : public StreamInfo::FilterState,
                            public std::enable_shared_from_this<UpstreamFilterState> {
public:
  // downstream_filter_state must be non-null; shared_request_lifespan_filter_state optionally
  // supplies the LifeSpan::Request storage shared across upstream attempts.
  explicit UpstreamFilterState(
      StreamInfo::FilterStateSharedPtr downstream_filter_state,
      StreamInfo::FilterStateSharedPtr shared_request_lifespan_filter_state = nullptr);

  // Binds the upstream L4 connection's FilterState (e.g. in UpstreamRequest::onPoolReady())
  // and merges any pre-pool LifeSpan::Connection entries into conn_filter_state.
  void bindUpstreamConnectionFilterState(StreamInfo::FilterStateSharedPtr conn_filter_state);

  // Merges attempt-local FilterChain entries into downstream_filter_state_ and enables
  // write-through for subsequent FilterChain writes.
  void commitToDownstream();

  bool committedToDownstream() const { return committed_; }

  // Returns a FilterState view over (local_filter_state_, upstream_conn_filter_state_) without
  // falling back to downstream_filter_state_. Used for UpstreamInfo::upstreamFilterState()
  // (%UPSTREAM_FILTER_STATE%).
  StreamInfo::FilterStateSharedPtr upstreamOnlyFilterState();

  // StreamInfo::FilterState
  // Note: stream_sharing is ignored on UpstreamFilterState because upstream transport socket
  // options and connection pool keys are finalized before the upstream HTTP filter chain is
  // created.
  void setData(
      absl::string_view data_name, std::shared_ptr<Object> data,
      StreamInfo::FilterState::LifeSpan life_span = StreamInfo::FilterState::LifeSpan::FilterChain,
      StreamInfo::StreamSharingMayImpactPooling stream_sharing =
          StreamInfo::StreamSharingMayImpactPooling::None) override;
  bool hasDataWithName(absl::string_view data_name) const override;
  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override;
  Object* getDataMutableGeneric(absl::string_view data_name) override;
  // Note: when adding an upstream filter, mutating a read-through downstream object in-place
  // mutates downstream state pre-commit; call setData() to isolate per-attempt state.
  std::shared_ptr<Object> getDataSharedMutableGeneric(absl::string_view data_name) override;
  bool hasDataAtOrAboveLifeSpan(StreamInfo::FilterState::LifeSpan life_span) const override;
  StreamInfo::FilterState::ObjectsPtr objectsSharedWithUpstreamConnection() const override;

  // Note: stream_sharing is ignored on UpstreamFilterState (see setData() above).
  void setIndexedData(
      StreamInfo::FilterStateIndex index, std::shared_ptr<Object> data,
      StreamInfo::FilterState::LifeSpan life_span = StreamInfo::FilterState::LifeSpan::FilterChain,
      StreamInfo::StreamSharingMayImpactPooling stream_sharing =
          StreamInfo::StreamSharingMayImpactPooling::None) override;
  const Object* getIndexedDataReadOnlyGeneric(StreamInfo::FilterStateIndex index) const override;
  Object* getIndexedDataMutableGeneric(StreamInfo::FilterStateIndex index) override;
  std::shared_ptr<Object>
  getIndexedDataSharedMutableGeneric(StreamInfo::FilterStateIndex index) override;
  bool hasIndexedData(StreamInfo::FilterStateIndex index) const override;

  StreamInfo::FilterState::LifeSpan lifeSpan() const override {
    return StreamInfo::FilterState::LifeSpan::FilterChain;
  }
  StreamInfo::FilterStateSharedPtr parent() const override {
    return downstream_filter_state_->parent();
  }

private:
  class UpstreamOnlyFilterStateView;

  StreamInfo::FilterState& ensureUpstreamConnFilterState();

  StreamInfo::FilterStateSharedPtr downstream_filter_state_;
  StreamInfo::FilterStateImpl local_filter_state_;
  std::shared_ptr<StreamInfo::FilterStateImpl> pre_pool_conn_filter_state_;
  StreamInfo::FilterStateSharedPtr upstream_conn_filter_state_;
  bool committed_{false};
};

} // namespace Router
} // namespace Envoy
