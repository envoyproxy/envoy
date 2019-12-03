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
  // FilterState
  void setData(absl::string_view data_name, std::shared_ptr<Object> data,
               FilterState::StateType state_type,
               FilterState::LifeSpan life_span = FilterState::LifeSpan::FilterChain) override;
  void copyInto(FilterState& other, FilterState::LifeSpan life_span) override;
  bool hasDataWithName(absl::string_view) const override;
  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override;
  Object* getDataMutableGeneric(absl::string_view data_name) override;

private:
  struct FilterObject {
    std::shared_ptr<Object> data_;
    FilterState::StateType state_type_;
    FilterState::LifeSpan life_span_;
  };

  absl::flat_hash_map<std::string, std::unique_ptr<FilterObject>> data_storage_;
};

} // namespace StreamInfo
} // namespace Envoy
