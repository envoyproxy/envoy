#include "source/common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"
#include "envoy/common/execution_context.h"

#include "source/common/common/compiled_string_map.h"
#include "source/common/common/macros.h"
#include "source/common/http/local_reply_owner_object.h"
#include "source/common/network/downstream_network_namespace.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/network/upstream_subject_alt_names.h"
#include "source/common/router/original_connect_port.h"

namespace Envoy {
namespace StreamInfo {
namespace {

template <typename T> bool checkObjectType(const FilterState::Object* data) {
  return dynamic_cast<const T*>(data) != nullptr;
}

} // namespace

absl::string_view FilterState::indexToName(FilterStateIndex index) {
  const size_t idx = static_cast<size_t>(index);
  if (idx >= FilterStateIndexCount) {
    return "";
  }
  static constexpr std::array<absl::string_view, FilterStateIndexCount> names = {
#define GENERATE_NAMES(enum_val, string_val, obj_type) string_val,
      FOR_EACH_FILTER_STATE_INDEX(GENERATE_NAMES)
#undef GENERATE_NAMES
  };
  return names[idx];
}

bool FilterState::isValidObjectForIndex(FilterStateIndex index, const Object* data) {
  const size_t idx = static_cast<size_t>(index);
  if (data == nullptr || idx >= FilterStateIndexCount) {
    return false;
  }
  using TypeChecker = bool (*)(const Object*);
  static constexpr std::array<TypeChecker, FilterStateIndexCount> checkers = {
#define GENERATE_CHECKER(enum_val, string_val, obj_type) &checkObjectType<obj_type>,
      FOR_EACH_FILTER_STATE_INDEX(GENERATE_CHECKER)
#undef GENERATE_CHECKER
  };
  return checkers[idx](data);
}

std::optional<FilterStateIndex> FilterState::nameToIndex(absl::string_view name) {
  static const auto map = []() {
    CompiledStringMap<uint32_t> m;
    std::vector<CompiledStringMap<uint32_t>::KV> contents;
    for (uint32_t i = 0; i < FilterStateIndexCount; ++i) {
      const FilterStateIndex idx = static_cast<FilterStateIndex>(i);
      contents.push_back({indexToName(idx), i + 1});
    }
    m.compile(std::move(contents));
    return m;
  }();
  const uint32_t val = map.find(name);
  if (val == 0) {
    return std::nullopt;
  }
  return static_cast<FilterStateIndex>(val - 1);
}

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
                              FilterState::LifeSpan life_span,
                              StreamSharingMayImpactPooling stream_sharing) {
  const auto index = FilterState::nameToIndex(data_name);
  if (index.has_value() && FilterState::isValidObjectForIndex(index.value(), data.get())) {
    setIndexedDataGeneric(index.value(), std::move(data), life_span, stream_sharing);
    return;
  }
  const bool has_local_indexed =
      index.has_value() && indexed_data_storage_[static_cast<size_t>(index.value())] != nullptr;
  if (life_span > life_span_) {
    if (has_local_indexed || hasDataWithNameInternally(data_name)) {
      IS_ENVOY_BUG(fmt::format("FilterStateAccessViolation: FilterState::setData<T> "
                               "called twice with "
                               "conflicting life_span on the same data_name: {}.",
                               data_name));
      return;
    }
    // Note if ancestor argument of ctor is not nullptr, parent will be created at the time of
    // construction directly and this call will be a no-op.
    // So we only need to consider the case where ancestor is nullptr.
    maybeCreateParent(nullptr);
    parent_->setData(data_name, std::move(data), life_span, stream_sharing);
    return;
  }
  if (parent_ && parent_->hasDataWithName(data_name)) {
    IS_ENVOY_BUG(
        fmt::format("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                    "conflicting life_span on the same data_name: {}.",
                    data_name));
    return;
  }

  if (has_local_indexed) {
    indexed_data_storage_[static_cast<size_t>(index.value())].reset();
    --indexed_count_;
  }
  std::unique_ptr<FilterStateImpl::FilterObject> filter_object(new FilterStateImpl::FilterObject());
  filter_object->data_ = std::move(data);
  filter_object->stream_sharing_ = stream_sharing;
  data_storage_[data_name] = std::move(filter_object);
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  const auto index = FilterState::nameToIndex(data_name);
  if (index.has_value() && hasIndexedData(index.value())) {
    return true;
  }
  return hasDataWithNameInternally(data_name) || (parent_ && parent_->hasDataWithName(data_name));
}

const FilterState::Object*
FilterStateImpl::getDataReadOnlyGeneric(absl::string_view data_name) const {
  const auto index = FilterState::nameToIndex(data_name);
  if (index.has_value()) {
    if (const Object* obj = getIndexedDataReadOnlyGeneric(index.value()); obj != nullptr) {
      return obj;
    }
  }
  if (!data_storage_.empty()) {
    const auto it = data_storage_.find(data_name);
    if (it != data_storage_.end()) {
      return it->second->data_.get();
    }
  }
  if (parent_) {
    return parent_->getDataReadOnlyGeneric(data_name);
  }
  return nullptr;
}

FilterState::Object* FilterStateImpl::getDataMutableGeneric(absl::string_view data_name) {
  return getDataSharedMutableGeneric(data_name).get();
}

std::shared_ptr<FilterState::Object>
FilterStateImpl::getDataSharedMutableGeneric(absl::string_view data_name) {
  const auto index = FilterState::nameToIndex(data_name);
  if (index.has_value()) {
    if (auto obj = getIndexedDataSharedMutableGeneric(index.value()); obj != nullptr) {
      return obj;
    }
  }
  if (!data_storage_.empty()) {
    const auto it = data_storage_.find(data_name);
    if (it != data_storage_.end()) {
      return it->second->data_;
    }
  }
  if (parent_) {
    return parent_->getDataSharedMutableGeneric(data_name);
  }
  return nullptr;
}

bool FilterStateImpl::hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const {
  if (life_span > life_span_) {
    return parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span);
  }
  const bool has_indexed_data = indexed_count_ > 0;
  const bool has_string_data = !data_storage_.empty();
  return has_string_data || has_indexed_data ||
         (parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span));
}

FilterState::ObjectsPtr FilterStateImpl::objectsSharedWithUpstreamConnection() const {
  auto objects = parent_ ? parent_->objectsSharedWithUpstreamConnection()
                         : std::make_unique<FilterState::Objects>();
  for (const auto& [name, object] : data_storage_) {
    switch (object->stream_sharing_) {
    case StreamSharingMayImpactPooling::SharedWithUpstreamConnection:
      objects->push_back({object->data_, object->stream_sharing_, name});
      break;
    case StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce:
      objects->push_back({object->data_, StreamSharingMayImpactPooling::None, name});
      break;
    default:
      break;
    }
  }
  for (size_t i = 0; i < FilterStateIndexCount; ++i) {
    const auto& object = indexed_data_storage_[i];
    if (object != nullptr) {
      const FilterStateIndex index = static_cast<FilterStateIndex>(i);
      switch (object->stream_sharing_) {
      case StreamSharingMayImpactPooling::SharedWithUpstreamConnection:
        objects->push_back(
            {object->data_, object->stream_sharing_, std::string(FilterState::indexToName(index))});
        break;
      case StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce:
        objects->push_back({object->data_, StreamSharingMayImpactPooling::None,
                            std::string(FilterState::indexToName(index))});
        break;
      default:
        break;
      }
    }
  }
  return objects;
}

bool FilterStateImpl::hasDataWithNameInternally(absl::string_view data_name) const {
  return data_storage_.contains(data_name);
}

void FilterStateImpl::setIndexedDataGeneric(FilterStateIndex index, std::shared_ptr<Object> data,
                                            FilterState::LifeSpan life_span,
                                            StreamSharingMayImpactPooling stream_sharing) {
  const size_t idx = static_cast<size_t>(index);
  ASSERT(idx < FilterStateIndexCount);
  if (data == nullptr) {
    return;
  }
  ASSERT(FilterState::isValidObjectForIndex(index, data.get()));
  if (life_span > life_span_) {
    if (indexed_data_storage_[idx] != nullptr ||
        (!data_storage_.empty() && hasDataWithNameInternally(FilterState::indexToName(index)))) {
      IS_ENVOY_BUG(fmt::format("FilterStateAccessViolation: FilterState::setIndexedData<T> "
                               "called twice with "
                               "conflicting life_span on index: {}.",
                               idx));
      return;
    }
    maybeCreateParent(nullptr);
    parent_->setIndexedDataGeneric(index, std::move(data), life_span, stream_sharing);
    return;
  }
  if (parent_ && (parent_->hasIndexedData(index) ||
                  parent_->hasDataWithName(FilterState::indexToName(index)))) {
    IS_ENVOY_BUG(
        fmt::format("FilterStateAccessViolation: FilterState::setIndexedData<T> called twice with "
                    "conflicting life_span on index: {}.",
                    idx));
    return;
  }

  if (!data_storage_.empty()) {
    data_storage_.erase(FilterState::indexToName(index));
  }
  if (indexed_data_storage_[idx] == nullptr) {
    ++indexed_count_;
  }
  auto filter_object = std::make_unique<FilterStateImpl::IndexedFilterObject>();
  filter_object->data_ = std::move(data);
  filter_object->stream_sharing_ = stream_sharing;
  indexed_data_storage_[idx] = std::move(filter_object);
}

const FilterState::Object*
FilterStateImpl::getIndexedDataReadOnlyGeneric(FilterStateIndex index) const {
  const size_t idx = static_cast<size_t>(index);
  if (idx >= FilterStateIndexCount) {
    return nullptr;
  }
  const auto& obj = indexed_data_storage_[idx];
  if (obj == nullptr) {
    if (parent_) {
      return parent_->getIndexedDataReadOnlyGeneric(index);
    }
    return nullptr;
  }
  return obj->data_.get();
}

FilterState::Object* FilterStateImpl::getIndexedDataMutableGeneric(FilterStateIndex index) {
  return getIndexedDataSharedMutableGeneric(index).get();
}

std::shared_ptr<FilterState::Object>
FilterStateImpl::getIndexedDataSharedMutableGeneric(FilterStateIndex index) {
  const size_t idx = static_cast<size_t>(index);
  if (idx >= FilterStateIndexCount) {
    return nullptr;
  }
  const auto& obj = indexed_data_storage_[idx];
  if (obj == nullptr) {
    if (parent_) {
      return parent_->getIndexedDataSharedMutableGeneric(index);
    }
    return nullptr;
  }
  return obj->data_;
}

bool FilterStateImpl::hasIndexedData(FilterStateIndex index) const {
  const size_t idx = static_cast<size_t>(index);
  if (idx >= FilterStateIndexCount) {
    return false;
  }
  return indexed_data_storage_[idx] != nullptr || (parent_ && parent_->hasIndexedData(index));
}

} // namespace StreamInfo
} // namespace Envoy
