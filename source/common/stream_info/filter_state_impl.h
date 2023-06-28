#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {

class FilterStateImpl : public FilterState {
public:
  FilterStateImpl(FilterState::LifeSpan life_span)
      : life_span_(life_span),
        data_storage_(
            InlineMapRegistry::createInlineMap<FilterStateInlineMapScope, FilterObject>()) {
    maybeCreateParent(ParentAccessMode::ReadOnly);
  }

  /**
   * @param ancestor a std::shared_ptr storing an already created ancestor.
   * @param life_span the life span this is handling.
   */
  FilterStateImpl(FilterStateSharedPtr ancestor, FilterState::LifeSpan life_span)
      : ancestor_(ancestor), life_span_(life_span),
        data_storage_(
            InlineMapRegistry::createInlineMap<FilterStateInlineMapScope, FilterObject>()) {
    maybeCreateParent(ParentAccessMode::ReadOnly);
  }

  using LazyCreateAncestor = std::pair<FilterStateSharedPtr, FilterState::LifeSpan>;
  /**
   * @param ancestor a std::pair storing an ancestor, that can be passed in as a way to lazy
   * initialize a FilterState that's owned by an object with bigger scope than this. This is to
   * avoid creating a FilterState that's empty in most cases.
   * @param life_span the life span this is handling.
   */
  FilterStateImpl(LazyCreateAncestor lazy_create_ancestor, FilterState::LifeSpan life_span)
      : ancestor_(lazy_create_ancestor), life_span_(life_span),
        data_storage_(
            InlineMapRegistry::createInlineMap<FilterStateInlineMapScope, FilterObject>()) {
    maybeCreateParent(ParentAccessMode::ReadOnly);
  }

  // FilterState
  void setData(
      absl::string_view data_name, std::shared_ptr<Object> data, FilterState::StateType state_type,
      FilterState::LifeSpan life_span = FilterState::LifeSpan::FilterChain,
      StreamSharingMayImpactPooling stream_sharing = StreamSharingMayImpactPooling::None) override;
  void setData(
      InlineKey data_key, std::shared_ptr<Object> data, StateType state_type,
      LifeSpan life_span = LifeSpan::FilterChain,
      StreamSharingMayImpactPooling stream_sharing = StreamSharingMayImpactPooling::None) override;

  bool hasDataWithName(absl::string_view) const override;
  bool hasDataGeneric(absl::string_view data_name) const override;
  bool hasDataGeneric(InlineKey data_key) const override;

  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override;
  const Object* getDataReadOnlyGeneric(InlineKey data_key) const override;

  Object* getDataMutableGeneric(absl::string_view data_name) override;
  Object* getDataMutableGeneric(InlineKey data_key) override;

  std::shared_ptr<Object> getDataSharedMutableGeneric(absl::string_view data_name) override;
  std::shared_ptr<Object> getDataSharedMutableGeneric(InlineKey data_key) override;

  bool hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const override;
  FilterState::ObjectsPtr objectsSharedWithUpstreamConnection() const override;

  FilterState::LifeSpan lifeSpan() const override { return life_span_; }
  FilterStateSharedPtr parent() const override { return parent_; }

private:
  template <class DataKeyType>
  void setDataInternal(DataKeyType data_key, std::shared_ptr<Object> data, StateType state_type,
                       LifeSpan life_span, StreamSharingMayImpactPooling stream_sharing) {

    if (life_span > life_span_) {
      if (hasDataInternal(data_key)) {
        IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                     "conflicting life_span on the same data_name.");
        return;
      }
      maybeCreateParent(ParentAccessMode::ReadWrite);
      parent_->setData(data_key, data, state_type, life_span, stream_sharing);
      return;
    }
    if (parent_ && parent_->hasDataGeneric(data_key)) {
      IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                   "conflicting life_span on the same data_name.");
      return;
    }
    const auto current = data_storage_->lookup(data_key);
    if (current.has_value()) {
      // We have another object with same data_name. Check for mutability
      // violations namely: readonly data cannot be overwritten, mutable data
      // cannot be overwritten by readonly data.
      if (current->state_type_ == FilterState::StateType::ReadOnly) {
        IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice on same "
                     "ReadOnly state.");
        return;
      }

      if (current->state_type_ != state_type) {
        IS_ENVOY_BUG("FilterStateAccessViolation: FilterState::setData<T> called twice with "
                     "different state types.");
        return;
      }
    }

    FilterStateImpl::FilterObject filter_object;
    filter_object.data_ = data;
    filter_object.state_type_ = state_type;
    filter_object.stream_sharing_ = stream_sharing;
    data_storage_->insert(data_key, std::move(filter_object));
  }

  // This only checks the local data_storage_ for data_name existence.
  template <class DataKeyType> bool hasDataInternal(DataKeyType data_key) const {
    return data_storage_->lookup(data_key).has_value();
  }

  template <class DataKeyType>
  const FilterState::Object* getDataReadOnlyGenericInternal(DataKeyType data_key) const {
    const auto current = data_storage_->lookup(data_key);

    if (!current.has_value()) {
      if (parent_) {
        return parent_->getDataReadOnlyGeneric(data_key);
      }
      return nullptr;
    }

    return current->data_.get();
  }

  template <class DataKeyType>
  std::shared_ptr<FilterState::Object> getDataSharedMutableGenericInternal(DataKeyType data_key) {
    const auto current = data_storage_->lookup(data_key);

    if (!current.has_value()) {
      if (parent_) {
        return parent_->getDataSharedMutableGeneric(data_key);
      }
      return nullptr;
    }

    if (current->state_type_ == FilterState::StateType::ReadOnly) {
      IS_ENVOY_BUG("FilterStateAccessViolation: FilterState accessed immutable data as mutable.");
      // To reduce the chances of a crash, allow the mutation in this case instead of returning a
      // nullptr.
    }

    return current->data_;
  }

  enum class ParentAccessMode { ReadOnly, ReadWrite };
  void maybeCreateParent(ParentAccessMode parent_access_mode);

  absl::variant<FilterStateSharedPtr, LazyCreateAncestor> ancestor_;
  FilterStateSharedPtr parent_;
  const FilterState::LifeSpan life_span_;

  InlineMapPtr<std::string, FilterObject> data_storage_;
};

} // namespace StreamInfo
} // namespace Envoy
