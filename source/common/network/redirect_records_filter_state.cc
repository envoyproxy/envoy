#include "common/network/redirect_records_filter_state.h"

#include "common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& RedirectRecordsFilterState::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.redirect_records");
}

} // namespace Network
} // namespace Envoy
