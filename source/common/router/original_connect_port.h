#pragma once

#include <cstdint>
#include <string>

#include "envoy/stream_info/filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Router {

/**
 * Original port from the authority header.
 * Defined in its own leaf header so StreamInfo::FilterStateImpl can validate
 * FilterStateIndex::OriginalConnectPort without depending on router config_lib.
 */
class OriginalConnectPort : public StreamInfo::FilterState::Object {
public:
  explicit OriginalConnectPort(uint32_t port) : port_(port) {}
  const uint32_t& value() const { return port_; }
  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.router.original_connect_port");
  }

private:
  const uint32_t port_;
};

} // namespace Router
} // namespace Envoy
