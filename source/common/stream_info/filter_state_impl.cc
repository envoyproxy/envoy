#include "common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace StreamInfo {

void FilterStateImpl::setData(absl::string_view data_name, std::shared_ptr<Object> data,
                              FilterState::StateType state_type, FilterState::LifeSpan life_span) {
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
  filter_object->life_span_ = life_span;
  data_storage_[data_name] = std::move(filter_object);
}

void FilterStateImpl::copyInto(FilterState& other, LifeSpan life_span) {
  for (const auto& filter_object : data_storage_) {
    if (filter_object.second->life_span_ >= life_span) {
      other.setData(filter_object.first, filter_object.second->data_,
                    filter_object.second->state_type_, filter_object.second->life_span_);
    }
  }
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  return data_storage_.count(data_name) > 0;
}

const FilterState::Object*
FilterStateImpl::getDataReadOnlyGeneric(absl::string_view data_name) const {
  const auto& it = data_storage_.find(data_name);

  if (it == data_storage_.end()) {
    throw EnvoyException("FilterState::getDataReadOnly<T> called for unknown data name.");
  }

  const FilterStateImpl::FilterObject* current = it->second.get();
  return current->data_.get();
}

FilterState::Object* FilterStateImpl::getDataMutableGeneric(absl::string_view data_name) {
  const auto& it = data_storage_.find(data_name);

  if (it == data_storage_.end()) {
    throw EnvoyException("FilterState::getDataMutable<T> called for unknown data name.");
  }

  FilterStateImpl::FilterObject* current = it->second.get();
  if (current->state_type_ == FilterState::StateType::ReadOnly) {
    throw EnvoyException(
        "FilterState::getDataMutable<T> tried to access immutable data as mutable.");
  }

  return current->data_.get();
}

} // namespace StreamInfo
} // namespace Envoy
