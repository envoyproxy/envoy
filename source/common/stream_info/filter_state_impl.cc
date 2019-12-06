#include "common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"
#include <type_traits>

namespace Envoy {
namespace StreamInfo {

FilterStateImpl::FilterStateImpl(std::shared_ptr<FilterState> parent,
                                 FilterState::LifeSpan life_span)
    : parent_(parent), life_span_(life_span) {
  if (life_span >= FilterState::LifeSpan::TopSpan) {
    parent_ = nullptr;
    return;
  }
  // Recursively create parent FilterState if no parent is provided or parent is not the immediate
  // parent.
  if (parent_ == nullptr || parent_->lifeSpan() != life_span_ + 1) {
    parent_ = std::make_shared<FilterStateImpl>(parent_, FilterState::LifeSpan(life_span_ + 1));
  }
}

void FilterStateImpl::setData(absl::string_view data_name, std::shared_ptr<Object> data,
                              FilterState::StateType state_type, FilterState::LifeSpan life_span) {
  if (life_span > life_span_) {
    if (hasDataWithNameInternally(data_name)) {
      throw EnvoyException(
          "FilterState::setData<T> called twice with conflicting life_span on the same data_name.");
    }
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

bool FilterStateImpl::hasDataAboveLifeSpan(FilterState::LifeSpan life_span) const {
  if (life_span > life_span_) {
    return parent_ && parent_->hasDataAboveLifeSpan(life_span);
  }
  return !data_storage_.empty() || (parent_ && parent_->hasDataAboveLifeSpan(life_span));
}

bool FilterStateImpl::hasDataWithNameInternally(absl::string_view data_name) const {
  return data_storage_.count(data_name) > 0;
}

} // namespace StreamInfo
} // namespace Envoy
