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
  void setData(absl::string_view data_name, std::unique_ptr<Object>&& data,
               FilterState::StateType state_type) override;
  bool hasDataWithName(absl::string_view) const override;
  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override;
  Object* getDataMutableGeneric(absl::string_view data_name) override;

private:
  struct FilterObject {
    std::unique_ptr<Object> data_;
    FilterState::StateType state_type_;
  };

  absl::flat_hash_map<std::string, std::unique_ptr<FilterObject>> data_storage_;
};

} // namespace StreamInfo
} // namespace Envoy
