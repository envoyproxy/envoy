#pragma once

#include <map>

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {

class FilterStateImpl : public FilterState {
public:
  // FilterState
  void setData(absl::string_view data_name, std::unique_ptr<Object>&& data) override;
  bool hasDataWithName(absl::string_view) const override;
  const Object* getDataGeneric(absl::string_view data_name) const override;

private:
  // The explicit non-type-specific comparator is necessary to allow use of find() method
  // with absl::string_view. See
  // https://stackoverflow.com/questions/20317413/what-are-transparent-comparators.
  std::map<std::string, std::unique_ptr<Object>, std::less<>> data_storage_;
};

} // namespace StreamInfo
} // namespace Envoy
