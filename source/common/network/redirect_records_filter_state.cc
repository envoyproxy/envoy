#include "common/network/redirect_records_filter_state.h"

#include "common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& Win32RedirectRecordsFilterState::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.win32_redirect_records");
}

} // namespace Network
} // namespace Envoy
