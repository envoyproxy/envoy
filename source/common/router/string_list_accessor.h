#pragma once

#include "third_party/absl/strings/str_join.h"
#include "third_party/envoy/src/envoy/stream_info/filter_state.h"

#include "third_party/absl/strings/string_view.h"

namespace Envoy {
namespace Router {

class StringListAccessor : public StreamInfo::FilterState::Object {
public:
  StringListAccessor(std::vector<std::string> value) :
    value_(std::move(value)) {}

  // FilterState::Object
  const std::vector<std::string> getList() const {
    return value_;
  }

  // StringListAccessor
  absl::optional<std::string> serializeAsString() const override {
    return absl::StrJoin(value_, ","); }

private:
  const std::vector<std::string> value_;
};

} // namespace Router
} // namespace Envoy
