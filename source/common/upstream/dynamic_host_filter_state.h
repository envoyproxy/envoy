#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Upstream {

class DynamicHostFilterState : public StreamInfo::FilterState::Object {
public:
  DynamicHostFilterState(absl::string_view host) : host_(host) {}
  const std::string& value() const { return host_; }
  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.clusters.dynamic_host_filter_state");
  }

private:
  const std::string host_;
};

} // namespace Upstream
} // namespace Envoy
