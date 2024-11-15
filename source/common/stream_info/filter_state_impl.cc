#include "source/common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace StreamInfo {

void FilterStateImpl::maybeCreateParent(FilterStateSharedPtr ancestor) {
  // If we already have a parent, or we're at the top span, we don't need to create
  // a parent.
  if (parent_ != nullptr || life_span_ >= FilterState::LifeSpan::TopSpan) {
    return;
  }

  const auto parent_life_span = FilterState::LifeSpan(life_span_ + 1);

  // No ancestor, or the provided ancestor has a shorter life span than the parent
  // we need to create, so we create a new parent.
  if (ancestor == nullptr || ancestor->lifeSpan() < parent_life_span) {
    parent_ = std::make_shared<FilterStateImpl>(parent_life_span);
    return;
  }

  // The ancestor is our immediate parent, use it.
  if (ancestor->lifeSpan() == parent_life_span) {
    parent_ = std::move(ancestor);
    return;
  }

  // The ancestor is not our immediate parent, so we need to create a chain of parents.
  parent_ = std::make_shared<FilterStateImpl>(std::move(ancestor), parent_life_span);
}

void FilterStateImpl::setData(absl::string_view data_name, std::shared_ptr<Object> data,
                              FilterState::StateType state_type, FilterState::LifeSpan life_span,
                              StreamSharingMayImpactPooling stream_sharing) {
  if (life_span > life_span_) {
    if (hasDataWithNameInternally(data_name)) {
      IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                   "conflicting life_span on the same data_name.");
      return;
    }
    // Note if ancestor argument of ctor is not nullptr, parent will be created at the time of
    // construction directly and this call will be a no-op.
    // So we only need to consider the case where ancestor is nullptr.
    maybeCreateParent(nullptr);
    parent_->setData(data_name, data, state_type, life_span, stream_sharing);
    return;
  }
  if (parent_ && parent_->hasDataWithName(data_name)) {
    IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                 "conflicting life_span on the same data_name.");
    return;
  }
  const auto& it = data_storage_.find(data_name);
  if (it != data_storage_.end()) {
    // We have another object with same data_name. Check for mutability
    // violations namely: readonly data cannot be overwritten, mutable data
    // cannot be overwritten by readonly data.
    const FilterStateImpl::FilterObject* current = it->second.get();
    if (current->state_type_ == FilterState::StateType::ReadOnly) {
      IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice on same "
                   "ReadOnly state.");
      return;
    }

    if (current->state_type_ != state_type) {
      IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                   "different state types.");
      return;
    }
  }

  std::unique_ptr<FilterStateImpl::FilterObject> filter_object(new FilterStateImpl::FilterObject());
  filter_object->data_ = data;
  filter_object->state_type_ = state_type;
  filter_object->stream_sharing_ = stream_sharing;
  data_storage_[data_name] = std::move(filter_object);
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  return hasDataWithNameInternally(data_name) || (parent_ && parent_->hasDataWithName(data_name));
}

const FilterState::Object*
FilterStateImpl::getDataReadOnlyGeneric(absl::string_view data_name) const {
  const auto it = data_storage_.find(data_name);

  if (it == data_storage_.end()) {
    if (parent_) {
      return parent_->getDataReadOnlyGeneric(data_name);
    }
    return nullptr;
  }

  const FilterStateImpl::FilterObject* current = it->second.get();
  return current->data_.get();
}

FilterState::Object* FilterStateImpl::getDataMutableGeneric(absl::string_view data_name) {
  return getDataSharedMutableGeneric(data_name).get();
}

std::shared_ptr<FilterState::Object>
FilterStateImpl::getDataSharedMutableGeneric(absl::string_view data_name) {
  const auto& it = data_storage_.find(data_name);

  if (it == data_storage_.end()) {
    if (parent_) {
      return parent_->getDataSharedMutableGeneric(data_name);
    }
    return nullptr;
  }

  FilterStateImpl::FilterObject* current = it->second.get();
  if (current->state_type_ == FilterState::StateType::ReadOnly) {
    IS_ENVOY_BUG("FilterStateAccessViolation: FilterState accessed immutable data as mutable.");
    // To reduce the chances of a crash, allow the mutation in this case instead of returning a
    // nullptr.
  }

  return current->data_;
}

bool FilterStateImpl::hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const {
  if (life_span > life_span_) {
    return parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span);
  }
  return !data_storage_.empty() || (parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span));
}

FilterState::ObjectsPtr FilterStateImpl::objectsSharedWithUpstreamConnection() const {
  auto objects = parent_ ? parent_->objectsSharedWithUpstreamConnection()
                         : std::make_unique<FilterState::Objects>();
  for (const auto& [name, object] : data_storage_) {
    switch (object->stream_sharing_) {
    case StreamSharingMayImpactPooling::SharedWithUpstreamConnection:
      objects->push_back({object->data_, object->state_type_, object->stream_sharing_, name});
      break;
    case StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce:
      objects->push_back(
          {object->data_, object->state_type_, StreamSharingMayImpactPooling::None, name});
      break;
    default:
      break;
    }
  }
  return objects;
}

bool FilterStateImpl::hasDataWithNameInternally(absl::string_view data_name) const {
  return data_storage_.contains(data_name);
}

} // namespace StreamInfo
} // namespace Envoy
