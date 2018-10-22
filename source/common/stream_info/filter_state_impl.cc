#include "common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace StreamInfo {

void FilterStateImpl::setData(absl::string_view data_name, std::unique_ptr<Object>&& data,
                              bool is_mutable) {
  // TODO(Google): Remove string conversion when fixed internally. Fixing
  // this TODO will also require an explicit cast from absl::string_view to
  // std::string in the data_storage_ index below; see
  // https://github.com/abseil/abseil-cpp/blob/master/absl/strings/string_view.h#L328
  const std::string name(data_name);
  const auto& it = data_storage_.find(name);

  if (it != data_storage_.end()) {
    // We have another object with same data_name. Check for mutability violations
    const FilterStateImpl::FilterObject* current = it->second.get();
    if (!current->mutable_) {
      throw EnvoyException("FilterState::setData<T> called twice with same name.");
    }
  }

  std::unique_ptr<FilterStateImpl::FilterObject> filter_object;
  filter_object.reset(new FilterStateImpl::FilterObject());
  filter_object->data_ = std::move(data);
  filter_object->mutable_ = is_mutable;
  data_storage_[name] = std::move(filter_object);
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  // TODO(Google): Remove string conversion when fixed internally.
  return data_storage_.count(std::string(data_name)) > 0;
}

const FilterState::Object* FilterStateImpl::getDataGeneric(absl::string_view data_name) const {
  // TODO(Google): Remove string conversion when fixed internally.
  const auto& it = data_storage_.find(std::string(data_name));

  if (it == data_storage_.end()) {
    throw EnvoyException("FilterState::getData<T> called for unknown data name.");
  }

  const FilterStateImpl::FilterObject* current = it->second.get();
  return current->data_.get();
}

FilterState::Object* FilterStateImpl::getDataGeneric(absl::string_view data_name) {
  // TODO(Google): Remove string conversion when fixed internally.
  const auto& it = data_storage_.find(std::string(data_name));

  if (it == data_storage_.end()) {
    throw EnvoyException("FilterState::getData<T> called for unknown data name.");
  }

  FilterStateImpl::FilterObject* current = it->second.get();
  if (!current->mutable_) {
    throw EnvoyException("FilterState::getData<T> tried to access immutable data as mutable.");
  }

  return current->data_.get();
}

} // namespace StreamInfo
} // namespace Envoy
