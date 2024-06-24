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
  FilterStateImpl(FilterState::LifeSpan life_span) : life_span_(life_span) {}

  /**
   * @param ancestor a std::shared_ptr storing an already created ancestor. If ancestor is
   * nullptr then the parent will be created lazily.
   * NOTE: ancestor may be the parent or a grandparent or even further up of the chain. It
   * may be different from the immediate parent.
   * @param life_span the life span this is handling.
   */
  FilterStateImpl(FilterStateSharedPtr ancestor, FilterState::LifeSpan life_span)
      : life_span_(life_span) {
    // If ancestor is nullptr, we will create the parent lazily, otherwise we will create
    // the parent immediately.
    if (ancestor != nullptr) {
      maybeCreateParent(std::move(ancestor));
    }
  }

  // FilterState
  void setData(
      absl::string_view data_name, std::shared_ptr<Object> data, FilterState::StateType state_type,
      FilterState::LifeSpan life_span = FilterState::LifeSpan::FilterChain,
      StreamSharingMayImpactPooling stream_sharing = StreamSharingMayImpactPooling::None) override;
  bool hasDataWithName(absl::string_view) const override;
  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override;
  Object* getDataMutableGeneric(absl::string_view data_name) override;
  std::shared_ptr<Object> getDataSharedMutableGeneric(absl::string_view data_name) override;
  bool hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const override;
  FilterState::ObjectsPtr objectsSharedWithUpstreamConnection() const override;

  FilterState::LifeSpan lifeSpan() const override { return life_span_; }
  FilterStateSharedPtr parent() const override { return parent_; }

private:
  // This only checks the local data_storage_ for data_name existence.
  bool hasDataWithNameInternally(absl::string_view data_name) const;
  void maybeCreateParent(FilterStateSharedPtr ancestor);

  FilterStateSharedPtr parent_;
  const FilterState::LifeSpan life_span_;
  absl::flat_hash_map<std::string, std::unique_ptr<FilterObject>> data_storage_;
};

} // namespace StreamInfo
} // namespace Envoy
