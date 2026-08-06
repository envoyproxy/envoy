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
                              FilterState::LifeSpan life_span,
                              StreamSharingMayImpactPooling stream_sharing) {
  auto index = FilterState::nameToIndex(data_name);
  if (index.has_value()) {
    setIndexedData(index.value(), data_name, data, life_span, stream_sharing);
    return;
  }
  if (life_span > life_span_) {
    if (hasDataWithNameInternally(data_name)) {
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
    parent_->setData(data_name, data, life_span, stream_sharing);
    return;
  }
  if (parent_ && parent_->hasDataWithName(data_name)) {
    IS_ENVOY_BUG(
        fmt::format("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                    "conflicting life_span on the same data_name: {}.",
                    data_name));
    return;
  }

  std::unique_ptr<FilterStateImpl::FilterObject> filter_object(new FilterStateImpl::FilterObject());
  filter_object->data_ = data;
  filter_object->stream_sharing_ = stream_sharing;
  if (data_storage_ == nullptr) {
    data_storage_ = std::make_unique<StringDataMap>();
  }
  (*data_storage_)[data_name] = std::move(filter_object);
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  auto index = FilterState::nameToIndex(data_name);
  if (index.has_value()) {
    return hasIndexedData(index.value());
  }
  return hasDataWithNameInternally(data_name) || (parent_ && parent_->hasDataWithName(data_name));
}

const FilterState::Object*
FilterStateImpl::getDataReadOnlyGeneric(absl::string_view data_name) const {
  auto index = FilterState::nameToIndex(data_name);
  if (index.has_value()) {
    return getIndexedDataReadOnlyGeneric(index.value());
  }
  if (data_storage_ == nullptr) {
    if (parent_) {
      return parent_->getDataReadOnlyGeneric(data_name);
    }
    return nullptr;
  }
  const auto it = data_storage_->find(data_name);

  if (it == data_storage_->end()) {
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
  auto index = FilterState::nameToIndex(data_name);
  if (index.has_value()) {
    return getIndexedDataSharedMutableGeneric(index.value());
  }
  if (data_storage_ == nullptr) {
    if (parent_) {
      return parent_->getDataSharedMutableGeneric(data_name);
    }
    return nullptr;
  }
  const auto& it = data_storage_->find(data_name);

  if (it == data_storage_->end()) {
    if (parent_) {
      return parent_->getDataSharedMutableGeneric(data_name);
    }
    return nullptr;
  }

  FilterStateImpl::FilterObject* current = it->second.get();
  return current->data_;
}

bool FilterStateImpl::hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const {
  if (life_span > life_span_) {
    return parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span);
  }
  bool has_indexed_data = false;
  for (const auto& obj : indexed_data_storage_) {
    if (obj != nullptr) {
      has_indexed_data = true;
      break;
    }
  }
  const bool has_string_data = data_storage_ != nullptr && !data_storage_->empty();
  return has_string_data || has_indexed_data ||
         (parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span));
}

FilterState::ObjectsPtr FilterStateImpl::objectsSharedWithUpstreamConnection() const {
  auto objects = parent_ ? parent_->objectsSharedWithUpstreamConnection()
                         : std::make_unique<FilterState::Objects>();
  if (data_storage_ != nullptr) {
    for (const auto& [name, object] : *data_storage_) {
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
  }
  for (const auto& object : indexed_data_storage_) {
    if (object != nullptr) {
      switch (object->stream_sharing_) {
      case StreamSharingMayImpactPooling::SharedWithUpstreamConnection:
        objects->push_back({object->data_, object->stream_sharing_, object->name_});
        break;
      case StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce:
        objects->push_back({object->data_, StreamSharingMayImpactPooling::None, object->name_});
        break;
      default:
        break;
      }
    }
  }
  return objects;
}

bool FilterStateImpl::hasDataWithNameInternally(absl::string_view data_name) const {
  return data_storage_ != nullptr && data_storage_->contains(data_name);
}

void FilterStateImpl::setIndexedData(FilterStateIndex index, absl::string_view data_name,
                                     std::shared_ptr<Object> data, FilterState::LifeSpan life_span,
                                     StreamSharingMayImpactPooling stream_sharing) {
  const size_t idx = static_cast<size_t>(index);
  if (idx >= static_cast<size_t>(FilterStateIndex::MaxIndex)) {
    return;
  }
  if (life_span > life_span_) {
    if (indexed_data_storage_[idx] != nullptr) {
      IS_ENVOY_BUG(fmt::format("FilterStateAccessViolation: FilterState::setIndexedData<T> "
                               "called twice with "
                               "conflicting life_span on index: {}.",
                               idx));
      return;
    }
    maybeCreateParent(nullptr);
    parent_->setIndexedData(index, data_name, data, life_span, stream_sharing);
    return;
  }
  if (parent_ && parent_->hasIndexedData(index)) {
    IS_ENVOY_BUG(
        fmt::format("FilterStateAccessViolation: FilterState::setIndexedData<T> called twice with "
                    "conflicting life_span on index: {}.",
                    idx));
    return;
  }

  std::unique_ptr<FilterStateImpl::FilterObject> filter_object(new FilterStateImpl::FilterObject());
  filter_object->data_ = data;
  filter_object->stream_sharing_ = stream_sharing;
  filter_object->name_ = std::string(data_name);
  indexed_data_storage_[idx] = std::move(filter_object);
}

const FilterState::Object*
FilterStateImpl::getIndexedDataReadOnlyGeneric(FilterStateIndex index) const {
  const size_t idx = static_cast<size_t>(index);
  if (idx >= static_cast<size_t>(FilterStateIndex::MaxIndex)) {
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
  if (idx >= static_cast<size_t>(FilterStateIndex::MaxIndex)) {
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
  if (idx >= static_cast<size_t>(FilterStateIndex::MaxIndex)) {
    return false;
  }
  return indexed_data_storage_[idx] != nullptr || (parent_ && parent_->hasIndexedData(index));
}

} // namespace StreamInfo
} // namespace Envoy
