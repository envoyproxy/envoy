#include "common/request_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace RequestInfo {

void FilterStateImpl::setData(absl::string_view data_name, std::unique_ptr<Object>&& data) {
  // TODO(Google): Remove string conversion when fixed internally.
  const std::string name(data_name);
  if (data_storage_.find(name) != data_storage_.end()) {
    throw EnvoyException("FilterState::setData<T> called twice with same name.");
  }
  // absl::string_view will not convert to std::string without an explicit case; see
  // https://github.com/abseil/abseil-cpp/blob/master/absl/strings/string_view.h#L328
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

} // namespace RequestInfo
} // namespace Envoy
