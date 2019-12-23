#include "common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace StreamInfo {

void FilterStateImpl::setData(absl::string_view data_name, std::shared_ptr<Object> data,
                              FilterState::StateType state_type, FilterState::LifeSpan life_span) {
  if (life_span > life_span_) {
    if (hasDataWithNameInternally(data_name)) {
      throw EnvoyException(
          "FilterState::setData<T> called twice with conflicting life_span on the same data_name.");
    }
    maybeCreateParent(ParentAccessMode::ReadWrite);
    parent_->setData(data_name, data, state_type, life_span);
    return;
  }
  if (parent_ && parent_->hasDataWithName(data_name)) {
    throw EnvoyException(
        "FilterState::setData<T> called twice with conflicting life_span on the same data_name.");
  }
  const auto& it = data_storage_.find(data_name);
  if (it != data_storage_.end()) {
    // We have another object with same data_name. Check for mutability
    // violations namely: readonly data cannot be overwritten. mutable data
    // cannot be overwritten by readonly data.
    const FilterStateImpl::FilterObject* current = it->second.get();
    if (current->state_type_ == FilterState::StateType::ReadOnly) {
      throw EnvoyException("FilterState::setData<T> called twice on same ReadOnly state.");
    }

    if (current->state_type_ != state_type) {
      throw EnvoyException("FilterState::setData<T> called twice with different state types.");
    }
  }

  std::unique_ptr<FilterStateImpl::FilterObject> filter_object(new FilterStateImpl::FilterObject());
  filter_object->data_ = data;
  filter_object->state_type_ = state_type;
  data_storage_[data_name] = std::move(filter_object);
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  return hasDataWithNameInternally(data_name) || (parent_ && parent_->hasDataWithName(data_name));
}

const FilterState::Object*
FilterStateImpl::getDataReadOnlyGeneric(absl::string_view data_name) const {
  const auto& it = data_storage_.find(data_name);

  if (it == data_storage_.end()) {
    if (parent_) {
      return &(parent_->getDataReadOnly<FilterState::Object>(data_name));
    }
    throw EnvoyException("FilterState::getDataReadOnly<T> called for unknown data name.");
  }

  const FilterStateImpl::FilterObject* current = it->second.get();
  return current->data_.get();
}

FilterState::Object* FilterStateImpl::getDataMutableGeneric(absl::string_view data_name) {
  const auto& it = data_storage_.find(data_name);

  if (it == data_storage_.end()) {
    if (parent_) {
      return &(parent_->getDataMutable<FilterState::Object>(data_name));
    }
    throw EnvoyException("FilterState::getDataMutable<T> called for unknown data name.");
  }

  FilterStateImpl::FilterObject* current = it->second.get();
  if (current->state_type_ == FilterState::StateType::ReadOnly) {
    throw EnvoyException(
        "FilterState::getDataMutable<T> tried to access immutable data as mutable.");
  }

  return current->data_.get();
}

bool FilterStateImpl::hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const {
  if (life_span > life_span_) {
    return parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span);
  }
  return !data_storage_.empty() || (parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span));
}

bool FilterStateImpl::hasDataWithNameInternally(absl::string_view data_name) const {
  return data_storage_.count(data_name) > 0;
}

void FilterStateImpl::maybeCreateParent(ParentAccessMode parent_access_mode) {
  if (parent_ != nullptr) {
    return;
  }
  if (life_span_ >= FilterState::LifeSpan::TopSpan) {
    return;
  }
  if (absl::holds_alternative<std::shared_ptr<FilterState>>(ancestor_)) {
    std::shared_ptr<FilterState> ancestor = absl::get<std::shared_ptr<FilterState>>(ancestor_);
    if (ancestor == nullptr || ancestor->lifeSpan() != life_span_ + 1) {
      parent_ = std::make_shared<FilterStateImpl>(ancestor, FilterState::LifeSpan(life_span_ + 1));
    } else {
      parent_ = ancestor;
    }
    return;
  }

  auto lazy_create_ancestor = absl::get<LazyCreateAncestor>(ancestor_);
  // If we're only going to read data from our parent, we don't need to create lazy ancestor,
  // because they're empty anyways.
  if (parent_access_mode == ParentAccessMode::ReadOnly && lazy_create_ancestor.first == nullptr) {
    return;
  }

  // Lazy ancestor is not our immediate parent.
  if (lazy_create_ancestor.second != life_span_ + 1) {
    parent_ = std::make_shared<FilterStateImpl>(lazy_create_ancestor,
                                                FilterState::LifeSpan(life_span_ + 1));
    return;
  }
  // Lazy parent is our immediate parent.
  if (lazy_create_ancestor.first == nullptr) {
    lazy_create_ancestor.first =
        std::make_shared<FilterStateImpl>(FilterState::LifeSpan(life_span_ + 1));
  }
  parent_ = lazy_create_ancestor.first;
}

} // namespace StreamInfo
} // namespace Envoy
