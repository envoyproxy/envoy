#pragma once

#include <memory>
#include <vector>

#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {

class FilterStateImpl : public FilterState {
public:
  FilterStateImpl(std::shared_ptr<FilterState> parent, FilterState::LifeSpan life_span);

  // FilterState
  void setData(absl::string_view data_name, std::shared_ptr<Object> data,
               FilterState::StateType state_type,
               FilterState::LifeSpan life_span = FilterState::LifeSpan::FilterChain) override;
  bool hasDataWithName(absl::string_view) const override;
  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override;
  Object* getDataMutableGeneric(absl::string_view data_name) override;

  FilterState::LifeSpan life_span() const override { return life_span_; }
  std::shared_ptr<FilterState> parent() const override { return parent_; }

private:
  struct FilterObject {
    std::shared_ptr<Object> data_;
    FilterState::StateType state_type_;
  };

  std::shared_ptr<FilterState> parent_;
  FilterState::LifeSpan life_span_;
  absl::flat_hash_map<std::string, std::unique_ptr<FilterObject>> data_storage_;
};

} // namespace StreamInfo
} // namespace Envoy
