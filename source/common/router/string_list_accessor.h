#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

// StringListAccessor extends StreamInfo::FilterState::Object to allow a list of
// strings to be stored in the filterState.
class StringListAccessor : public StreamInfo::FilterState::Object {
public:
  StringListAccessor(const std::vector<std::string>& value) : value_(value) {}

  // Returns a list of strings stored in the filterState.
  const std::vector<std::string>& getList() const { return value_; }

  // StringListAccessor
  absl::optional<std::string> serializeAsString() const override {
    return absl::StrJoin(value_, ",");
  }

private:
  const std::vector<std::string> value_;
};

} // namespace Router
} // namespace Envoy
