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
  FilterStateImpl(FilterState::LifeSpan life_span) : life_span_(life_span) {
    maybeCreateParent(ParentAccessMode::ReadOnly);
  }

  /**
   * @param ancestor a std::shared_ptr storing an already created ancestor.
   * @param life_span the life span this is handling.
   */
  FilterStateImpl(FilterStateSharedPtr ancestor, FilterState::LifeSpan life_span)
      : ancestor_(ancestor), life_span_(life_span) {
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
      : ancestor_(lazy_create_ancestor), life_span_(life_span) {
    maybeCreateParent(ParentAccessMode::ReadOnly);
  }

  // FilterState
  void setData(absl::string_view data_name, std::shared_ptr<Object> data,
               FilterState::StateType state_type,
               FilterState::LifeSpan life_span = FilterState::LifeSpan::FilterChain) override;
  bool hasDataWithName(absl::string_view) const override;
  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override;
  Object* getDataMutableGeneric(absl::string_view data_name) override;
  bool hasDataAtOrAboveLifeSpan(FilterState::LifeSpan life_span) const override;

  FilterState::LifeSpan lifeSpan() const override { return life_span_; }
  FilterStateSharedPtr parent() const override { return parent_; }

private:
  // This only checks the local data_storage_ for data_name existence.
  bool hasDataWithNameInternally(absl::string_view data_name) const;
  enum class ParentAccessMode { ReadOnly, ReadWrite };
  void maybeCreateParent(ParentAccessMode parent_access_mode);

  struct FilterObject {
    std::shared_ptr<Object> data_;
    FilterState::StateType state_type_;
  };

  absl::variant<FilterStateSharedPtr, LazyCreateAncestor> ancestor_;
  FilterStateSharedPtr parent_;
  const FilterState::LifeSpan life_span_;
  absl::flat_hash_map<std::string, std::unique_ptr<FilterObject>> data_storage_;
};

} // namespace StreamInfo
} // namespace Envoy
