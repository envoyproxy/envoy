#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {

/**
 * Original Requested Server Name
 */
class OriginalRequestedServerName : public FilterState::Object {
public:
  OriginalRequestedServerName(absl::string_view server_name) : server_name_(server_name) {}
  const std::string& value() const { return server_name_; }
  static const std::string Key;

private:
  const std::string server_name_;
};

} // namespace StreamInfo
} // namespace Envoy
