#include "source/common/router/upstream_filter_state.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "envoy/stream_info/filter_state.h"

#include "source/common/common/assert.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

// FilterState view backing UpstreamInfo::upstreamFilterState() (%UPSTREAM_FILTER_STATE%).
// Reads check attempt-local FilterChain and shared upstream Request storage (local_filter_state_)
// followed by the backend connection's FilterState (upstream_conn_filter_state_, null until bound
// or lazily allocated by a pre-pool >= LifeSpan::Connection write) without falling back to
// downstream_filter_state_. Writes at >= LifeSpan::Connection target
// upstream_conn_filter_state_, while FilterChain and Request writes delegate to parent_.
class UpstreamFilterState::UpstreamOnlyFilterStateView : public StreamInfo::FilterState {
public:
  explicit UpstreamOnlyFilterStateView(std::shared_ptr<UpstreamFilterState> parent)
      : parent_(std::move(parent)) {}

  void setData(absl::string_view data_name, std::shared_ptr<Object> data,
               StreamInfo::FilterState::LifeSpan life_span,
               StreamInfo::StreamSharingMayImpactPooling /*stream_sharing*/) override {
    if (life_span >= StreamInfo::FilterState::LifeSpan::Connection) {
      parent_->ensureUpstreamConnFilterState().setData(
          data_name, std::move(data), StreamInfo::FilterState::LifeSpan::Connection,
          StreamInfo::StreamSharingMayImpactPooling::None);
      return;
    }
    parent_->setData(data_name, std::move(data), life_span,
                     StreamInfo::StreamSharingMayImpactPooling::None);
  }

  bool hasDataWithName(absl::string_view data_name) const override {
    const auto* conn = parent_->upstream_conn_filter_state_.get();
    return parent_->local_filter_state_.hasDataWithName(data_name) ||
           (conn != nullptr && conn->hasDataWithName(data_name));
  }

  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override {
    if (const Object* obj = parent_->local_filter_state_.getDataReadOnlyGeneric(data_name);
        obj != nullptr) {
      return obj;
    }
    const auto* conn = parent_->upstream_conn_filter_state_.get();
    return conn != nullptr ? conn->getDataReadOnlyGeneric(data_name) : nullptr;
  }

  Object* getDataMutableGeneric(absl::string_view data_name) override {
    return getDataSharedMutableGeneric(data_name).get();
  }

  std::shared_ptr<Object> getDataSharedMutableGeneric(absl::string_view data_name) override {
    if (auto obj = parent_->local_filter_state_.getDataSharedMutableGeneric(data_name);
        obj != nullptr) {
      return obj;
    }
    auto* conn = parent_->upstream_conn_filter_state_.get();
    return conn != nullptr ? conn->getDataSharedMutableGeneric(data_name) : nullptr;
  }

  bool hasDataAtOrAboveLifeSpan(StreamInfo::FilterState::LifeSpan life_span) const override {
    const auto* conn = parent_->upstream_conn_filter_state_.get();
    return parent_->local_filter_state_.hasDataAtOrAboveLifeSpan(life_span) ||
           (conn != nullptr && conn->hasDataAtOrAboveLifeSpan(life_span));
  }

  StreamInfo::FilterState::ObjectsPtr objectsSharedWithUpstreamConnection() const override {
    const auto* conn = parent_->upstream_conn_filter_state_.get();
    return conn != nullptr ? conn->objectsSharedWithUpstreamConnection()
                           : std::make_unique<StreamInfo::FilterState::Objects>();
  }

  void setIndexedData(StreamInfo::FilterStateIndex index, std::shared_ptr<Object> data,
                      StreamInfo::FilterState::LifeSpan life_span,
                      StreamInfo::StreamSharingMayImpactPooling /*stream_sharing*/) override {
    if (static_cast<size_t>(index) < StreamInfo::FilterStateIndexCount) {
      // FilterStateImpl::setData() routes indexed names back into indexed_data_storage_.
      setData(StreamInfo::FilterState::indexToName(index), std::move(data), life_span,
              StreamInfo::StreamSharingMayImpactPooling::None);
    }
  }

  const Object* getIndexedDataReadOnlyGeneric(StreamInfo::FilterStateIndex index) const override {
    if (const Object* obj = parent_->local_filter_state_.getIndexedDataReadOnlyGeneric(index);
        obj != nullptr) {
      return obj;
    }
    const auto* conn = parent_->upstream_conn_filter_state_.get();
    return conn != nullptr ? conn->getIndexedDataReadOnlyGeneric(index) : nullptr;
  }

  Object* getIndexedDataMutableGeneric(StreamInfo::FilterStateIndex index) override {
    return getIndexedDataSharedMutableGeneric(index).get();
  }

  std::shared_ptr<Object>
  getIndexedDataSharedMutableGeneric(StreamInfo::FilterStateIndex index) override {
    if (auto obj = parent_->local_filter_state_.getIndexedDataSharedMutableGeneric(index);
        obj != nullptr) {
      return obj;
    }
    auto* conn = parent_->upstream_conn_filter_state_.get();
    return conn != nullptr ? conn->getIndexedDataSharedMutableGeneric(index) : nullptr;
  }

  bool hasIndexedData(StreamInfo::FilterStateIndex index) const override {
    const auto* conn = parent_->upstream_conn_filter_state_.get();
    return parent_->local_filter_state_.hasIndexedData(index) ||
           (conn != nullptr && conn->hasIndexedData(index));
  }

  StreamInfo::FilterState::LifeSpan lifeSpan() const override {
    return StreamInfo::FilterState::LifeSpan::FilterChain;
  }

  StreamInfo::FilterStateSharedPtr parent() const override {
    return parent_->upstream_conn_filter_state_;
  }

private:
  std::shared_ptr<UpstreamFilterState> parent_;
};

UpstreamFilterState::UpstreamFilterState(
    StreamInfo::FilterStateSharedPtr downstream_filter_state,
    StreamInfo::FilterStateSharedPtr shared_request_lifespan_filter_state)
    : downstream_filter_state_(std::move(downstream_filter_state)),
      local_filter_state_(shared_request_lifespan_filter_state != nullptr
                              ? std::move(shared_request_lifespan_filter_state)
                              : std::make_shared<StreamInfo::FilterStateImpl>(
                                    StreamInfo::FilterState::LifeSpan::Request),
                          StreamInfo::FilterState::LifeSpan::FilterChain) {
  ASSERT(downstream_filter_state_ != nullptr);
  ASSERT(local_filter_state_.parent() != nullptr &&
         local_filter_state_.parent()->lifeSpan() == StreamInfo::FilterState::LifeSpan::Request);
}

StreamInfo::FilterStateSharedPtr UpstreamFilterState::upstreamOnlyFilterState() {
  ASSERT(!weak_from_this().expired());
  return std::make_shared<UpstreamOnlyFilterStateView>(shared_from_this());
}

StreamInfo::FilterState& UpstreamFilterState::ensureUpstreamConnFilterState() {
  if (upstream_conn_filter_state_ == nullptr) {
    pre_pool_conn_filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::Connection);
    upstream_conn_filter_state_ = pre_pool_conn_filter_state_;
  }
  return *upstream_conn_filter_state_;
}

void UpstreamFilterState::bindUpstreamConnectionFilterState(
    StreamInfo::FilterStateSharedPtr conn_filter_state) {
  if (conn_filter_state == nullptr || conn_filter_state == upstream_conn_filter_state_) {
    return;
  }
  if (pre_pool_conn_filter_state_ != nullptr) {
    pre_pool_conn_filter_state_->mergeInto(*conn_filter_state,
                                           StreamInfo::FilterState::LifeSpan::Connection);
    pre_pool_conn_filter_state_.reset();
  }
  upstream_conn_filter_state_ = std::move(conn_filter_state);
}

void UpstreamFilterState::commitToDownstream() {
  if (committed_) {
    return;
  }
  committed_ = true;
  local_filter_state_.mergeInto(*downstream_filter_state_,
                                StreamInfo::FilterState::LifeSpan::FilterChain);
}

void UpstreamFilterState::setData(absl::string_view data_name, std::shared_ptr<Object> data,
                                  StreamInfo::FilterState::LifeSpan life_span,
                                  StreamInfo::StreamSharingMayImpactPooling /*stream_sharing*/) {
  if (life_span >= StreamInfo::FilterState::LifeSpan::Connection) {
    if (local_filter_state_.hasDataWithName(data_name)) {
      IS_ENVOY_BUG(absl::StrCat("FilterStateAccessViolation: FilterState::setData<T> "
                                "called twice with conflicting life_span on the same "
                                "data_name: ",
                                data_name, "."));
      return;
    }
    downstream_filter_state_->setData(data_name, std::move(data),
                                      StreamInfo::FilterState::LifeSpan::Connection,
                                      StreamInfo::StreamSharingMayImpactPooling::None);
    return;
  }

  if (life_span == StreamInfo::FilterState::LifeSpan::Request) {
    if (local_filter_state_.hasDataWithName(data_name) &&
        !local_filter_state_.parent()->hasDataWithName(data_name)) {
      IS_ENVOY_BUG(absl::StrCat("FilterStateAccessViolation: FilterState::setData<T> "
                                "called twice with conflicting life_span on the same "
                                "data_name: ",
                                data_name, "."));
      return;
    }
    downstream_filter_state_->setData(data_name, data, StreamInfo::FilterState::LifeSpan::Request,
                                      StreamInfo::StreamSharingMayImpactPooling::None);
    // Only mirror into shared upstream Request storage if downstream accepted the write.
    if (downstream_filter_state_->hasDataWithName(data_name) &&
        downstream_filter_state_->getDataReadOnlyGeneric(data_name) == data.get()) {
      local_filter_state_.setData(data_name, std::move(data),
                                  StreamInfo::FilterState::LifeSpan::Request,
                                  StreamInfo::StreamSharingMayImpactPooling::None);
    }
    return;
  }

  local_filter_state_.setData(data_name, data, life_span,
                              StreamInfo::StreamSharingMayImpactPooling::None);
  // Skip write-through if local_filter_state_.setData() rejected a Request lifespan collision.
  if (committed_ && !local_filter_state_.parent()->hasDataWithName(data_name)) {
    downstream_filter_state_->setData(data_name, std::move(data), life_span,
                                      StreamInfo::StreamSharingMayImpactPooling::None);
  }
}

bool UpstreamFilterState::hasDataWithName(absl::string_view data_name) const {
  return local_filter_state_.hasDataWithName(data_name) ||
         downstream_filter_state_->hasDataWithName(data_name);
}

const StreamInfo::FilterState::Object*
UpstreamFilterState::getDataReadOnlyGeneric(absl::string_view data_name) const {
  const Object* obj = local_filter_state_.getDataReadOnlyGeneric(data_name);
  return obj != nullptr ? obj : downstream_filter_state_->getDataReadOnlyGeneric(data_name);
}

StreamInfo::FilterState::Object*
UpstreamFilterState::getDataMutableGeneric(absl::string_view data_name) {
  return getDataSharedMutableGeneric(data_name).get();
}

std::shared_ptr<StreamInfo::FilterState::Object>
UpstreamFilterState::getDataSharedMutableGeneric(absl::string_view data_name) {
  auto obj = local_filter_state_.getDataSharedMutableGeneric(data_name);
  return obj != nullptr ? obj : downstream_filter_state_->getDataSharedMutableGeneric(data_name);
}

bool UpstreamFilterState::hasDataAtOrAboveLifeSpan(
    StreamInfo::FilterState::LifeSpan life_span) const {
  return local_filter_state_.hasDataAtOrAboveLifeSpan(life_span) ||
         downstream_filter_state_->hasDataAtOrAboveLifeSpan(life_span);
}

StreamInfo::FilterState::ObjectsPtr
UpstreamFilterState::objectsSharedWithUpstreamConnection() const {
  return downstream_filter_state_->objectsSharedWithUpstreamConnection();
}

void UpstreamFilterState::setIndexedData(
    StreamInfo::FilterStateIndex index, std::shared_ptr<Object> data,
    StreamInfo::FilterState::LifeSpan life_span,
    StreamInfo::StreamSharingMayImpactPooling /*stream_sharing*/) {
  if (static_cast<size_t>(index) < StreamInfo::FilterStateIndexCount) {
    // FilterStateImpl::setData() routes indexed names back into indexed_data_storage_.
    setData(StreamInfo::FilterState::indexToName(index), std::move(data), life_span,
            StreamInfo::StreamSharingMayImpactPooling::None);
  }
}

const StreamInfo::FilterState::Object*
UpstreamFilterState::getIndexedDataReadOnlyGeneric(StreamInfo::FilterStateIndex index) const {
  const Object* obj = local_filter_state_.getIndexedDataReadOnlyGeneric(index);
  return obj != nullptr ? obj : downstream_filter_state_->getIndexedDataReadOnlyGeneric(index);
}

StreamInfo::FilterState::Object*
UpstreamFilterState::getIndexedDataMutableGeneric(StreamInfo::FilterStateIndex index) {
  return getIndexedDataSharedMutableGeneric(index).get();
}

std::shared_ptr<StreamInfo::FilterState::Object>
UpstreamFilterState::getIndexedDataSharedMutableGeneric(StreamInfo::FilterStateIndex index) {
  auto obj = local_filter_state_.getIndexedDataSharedMutableGeneric(index);
  return obj != nullptr ? obj : downstream_filter_state_->getIndexedDataSharedMutableGeneric(index);
}

bool UpstreamFilterState::hasIndexedData(StreamInfo::FilterStateIndex index) const {
  return local_filter_state_.hasIndexedData(index) ||
         downstream_filter_state_->hasIndexedData(index);
}

} // namespace Router
} // namespace Envoy
