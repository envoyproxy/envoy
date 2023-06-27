#include "source/common/stream_info/filter_state_impl.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace StreamInfo {

void FilterStateImpl::setData(absl::string_view data_name, std::shared_ptr<Object> data,
                              FilterState::StateType state_type, FilterState::LifeSpan life_span,
                              StreamSharingMayImpactPooling stream_sharing) {
  setDataInternal(data_name, data, state_type, life_span, stream_sharing);
}

void FilterStateImpl::setData(InlineKey data_key, std::shared_ptr<Object> data,
                              StateType state_type, LifeSpan life_span,
                              StreamSharingMayImpactPooling stream_sharing) {
  setDataInternal(data_key, data, state_type, life_span, stream_sharing);
}

bool FilterStateImpl::hasDataWithName(absl::string_view data_name) const {
  return hasDataInternal(data_name) || (parent_ && parent_->hasDataWithName(data_name));
}
bool FilterStateImpl::hasDataGeneric(absl::string_view data_name) const {
  return hasDataInternal(data_name) || (parent_ && parent_->hasDataGeneric(data_name));
}
bool FilterStateImpl::hasDataGeneric(InlineKey data_key) const {
  return hasDataInternal(data_key) || (parent_ && parent_->hasDataGeneric(data_key));
}

const FilterState::Object*
FilterStateImpl::getDataReadOnlyGeneric(absl::string_view data_name) const {
  return getDataReadOnlyGenericInternal(data_name);
}
const FilterState::Object* FilterStateImpl::getDataReadOnlyGeneric(InlineKey data_key) const {
  return getDataReadOnlyGenericInternal(data_key);
}

FilterState::Object* FilterStateImpl::getDataMutableGeneric(absl::string_view data_name) {
  return getDataSharedMutableGeneric(data_name).get();
}
FilterState::Object* FilterStateImpl::getDataMutableGeneric(InlineKey data_key) {
  return getDataSharedMutableGeneric(data_key).get();
}

std::shared_ptr<FilterState::Object>
FilterStateImpl::getDataSharedMutableGeneric(absl::string_view data_name) {
  return getDataSharedMutableGenericInternal(data_name);
}
std::shared_ptr<FilterState::Object>
FilterStateImpl::getDataSharedMutableGeneric(InlineKey data_key) {
  return getDataSharedMutableGenericInternal(data_key);
}

bool FilterStateImpl::hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const {
  if (life_span > life_span_) {
    return parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span);
  }
  return !data_storage_->empty() || (parent_ && parent_->hasDataAtOrAboveLifeSpan(life_span));
}

FilterState::ObjectsPtr FilterStateImpl::objectsSharedWithUpstreamConnection() const {
  auto objects = parent_ ? parent_->objectsSharedWithUpstreamConnection()
                         : std::make_unique<FilterState::Objects>();

  data_storage_->iterate([&objects](const std::string& name, const FilterObject& object) -> bool {
    switch (object.stream_sharing_) {
    case StreamSharingMayImpactPooling::SharedWithUpstreamConnection:
      objects->push_back(
          {object.data_, object.state_type_, object.stream_sharing_, std::string(name)});
      break;
    case StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce:
      objects->push_back({object.data_, object.state_type_, StreamSharingMayImpactPooling::None,
                          std::string(name)});
      break;
    default:
      break;
    }
    return true;
  });

  return objects;
}

void FilterStateImpl::maybeCreateParent(ParentAccessMode parent_access_mode) {
  if (parent_ != nullptr) {
    return;
  }
  if (life_span_ >= FilterState::LifeSpan::TopSpan) {
    return;
  }
  if (absl::holds_alternative<FilterStateSharedPtr>(ancestor_)) {
    FilterStateSharedPtr ancestor = absl::get<FilterStateSharedPtr>(ancestor_);
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
