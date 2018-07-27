#include "common/request_info/dynamic_metadata_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace RequestInfo {

DynamicMetadataImpl::~DynamicMetadataImpl() {
  for (auto& it : data_storage_) {
    if (it.second.destructor_) {
      it.second.destructor_(it.second.ptr_);
    }
  }
}

void DynamicMetadataImpl::setDataGeneric(absl::string_view data_name, size_t type_id, void* data,
                                         void (*destructor)(void*)) {
  if (data_storage_.find(data_name) != data_storage_.end()) {
    (*destructor)(data);
    throw EnvoyException("DynamicMetadata::setData<T> called twice with same name.");
  }

  data_storage_[static_cast<std::string>(data_name)] = {type_id, data, destructor};
}

void* DynamicMetadataImpl::getDataGeneric(absl::string_view data_name, size_t type_id) const {
  const auto& it = data_storage_.find(data_name);

  if (it == data_storage_.end()) {
    throw EnvoyException("DynamicMetadata::getData<T> called for unknown data name.");
  }

  if (it->second.typeid_ != type_id) {
    throw EnvoyException(
        "DynamicMetadata::getData<T> called with different type than name originally set with.");
  }

  return it->second.ptr_;
}

bool DynamicMetadataImpl::hasDataGeneric(absl::string_view data_name, size_t type_id) const {
  const auto& it = data_storage_.find(data_name);

  return (it != data_storage_.end() && it->second.typeid_ == type_id);
}

bool DynamicMetadataImpl::hasDataWithName(absl::string_view data_name) const {
  return (data_storage_.find(data_name) != data_storage_.end());
}

} // namespace RequestInfo
} // namespace Envoy
