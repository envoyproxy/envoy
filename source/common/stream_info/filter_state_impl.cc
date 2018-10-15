#include "common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace StreamInfo {

void FilterStateImpl::setData(absl::string_view data_name, std::unique_ptr<Object>&& data) {
  // TODO(Google): Remove string conversion when fixed internally. Fixing
  // this TODO will also require an explicit cast from absl::string_view to
  // std::string in the data_storage_ index below; see
  // https://github.com/abseil/abseil-cpp/blob/master/absl/strings/string_view.h#L328
  const std::string name(data_name);
  if (data_storage_.find(name) != data_storage_.end()) {
    throw EnvoyException("FilterState::setData<T> called twice with same name.");
  }
  data_storage_[name] = std::move(data);
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
  return it->second.get();
}

void FilterStateImpl::addToListGeneric(absl::string_view data_name,
                                       std::unique_ptr<Object>&& data) {
  const std::string name(data_name);
  if (list_storage_.find(name) == list_storage_.end()) {
    list_storage_[name] = std::vector<std::unique_ptr<FilterState::Object>>();
  }

  list_storage_[name].push_back(std::move(data));
}

const std::vector<std::unique_ptr<FilterState::Object>>*
FilterStateImpl::getList(absl::string_view data_name) const {
  const auto& it = list_storage_.find(std::string(data_name));

  if (it == list_storage_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool FilterStateImpl::hasListWithName(absl::string_view data_name) const {
  return list_storage_.count(std::string(data_name)) > 0;
}

} // namespace StreamInfo
} // namespace Envoy
