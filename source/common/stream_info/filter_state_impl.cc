#include "common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace StreamInfo {

void FilterStateImpl::setData(absl::string_view data_name, std::unique_ptr<Object>&& data,
                              FilterState::StateType state_type) {
  // TODO(Google): Remove string conversion when fixed internally. Fixing
  // this TODO will also require an explicit cast from absl::string_view to
  // std::string in the data_storage_ index below; see
  // https://github.com/abseil/abseil-cpp/blob/master/absl/strings/string_view.h#L328
  const std::string name(data_name);
  const auto& it = data_storage_.find(name);

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
  filter_object->data_ = std::move(data);
  filter_object->state_type_ = state_type;
  data_storage_[name] = std::move(filter_object);
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  // TODO(Google): Remove string conversion when fixed internally.
  return data_storage_.count(std::string(data_name)) > 0;
}

const FilterState::Object*
FilterStateImpl::getDataReadOnlyGeneric(absl::string_view data_name) const {
  // TODO(Google): Remove string conversion when fixed internally.
  const auto& it = data_storage_.find(std::string(data_name));

  if (it == data_storage_.end()) {
    throw EnvoyException("FilterState::getDataReadOnly<T> called for unknown data name.");
  }

  const FilterStateImpl::FilterObject* current = it->second.get();
  return current->data_.get();
}

FilterState::Object* FilterStateImpl::getDataMutableGeneric(absl::string_view data_name) {
  // TODO(Google): Remove string conversion when fixed internally.
  const auto& it = data_storage_.find(std::string(data_name));

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
